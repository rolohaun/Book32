#include "AppReader.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_reader.h"
#include "Book32FS.h"
#include "WebMgr.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <map>

static const char* READER_PROGRESS_PATH = "/reader_progress.json";

static String normalizedBookName(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    slash = name.lastIndexOf('\\');
    if (slash >= 0) name = name.substring(slash + 1);
    return name;
}

static int textWidthForFont(Book32Display& display, const char* text, const GFXfont* font) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(font);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

static void drawTextWithFont(Book32Display& display, const char* text, int x, int y, const GFXfont* font, uint16_t color) {
    display.setFont(font);
    display.setTextColor(color);
    display.setTextSize(1);
    display.setCursor(x, y);
    display.print(text);
}

static String titleFromFilename(String name) {
    name = normalizedBookName(name);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    name.replace('_', ' ');
    name.trim();
    return name;
}

static void loadBookMetadata(std::map<String, String>& metadata) {
    metadata.clear();

    File file;
    if (SystemFS.exists("/books_meta.json")) {
        file = SystemFS.open("/books_meta.json", "r");
    } else if (EbookFS.exists("/books_meta.json")) {
        file = EbookFS.open("/books_meta.json", "r");
    }

    if (!file) return;

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair pair : obj) {
        metadata[String(pair.key().c_str())] = pair.value().as<String>();
    }
}

struct LibraryDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static LibraryDirtyRect libraryItemRect(int index, int screenW) {
    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int ITEM_HEIGHT = 110;
    if (index < 0) {
        return {14, HEADER_H, screenW - 28, BACK_ITEM_HEIGHT + 4};
    }
    return {14, HEADER_H + BACK_ITEM_HEIGHT + (index * ITEM_HEIGHT), screenW - 28, ITEM_HEIGHT + 4};
}

static LibraryDirtyRect unionLibraryRect(LibraryDirtyRect a, LibraryDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

AppReader::AppReader() {
    _state = VIEW_LIBRARY;
    _selectedBookIndex = 0;
    _scrollOffset = 0;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _resumeSavedBookOnStart = false;
    _previousBookIndex = 0;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _currentChapter = 0;
    _currentPage = 0;
    _needsRedraw = true;
    _currentPageRender = {0, 0, false, 0, 0};
    _currentPageRenderValid = false;
    _pageTurnsSinceRefresh = 0;
    _totalBookPages = 0;
    _refreshEveryNPages = 10; // Default to full refresh every 10 pages
    _fontSizePt = 9;          // Default body size (small)
    _useOpenSans = false;     // Preserve the original reader font by default
    _readingFirstDraw = true;
    loadSettings();
}

void AppReader::loadSettings() {
    // Try EbookFS first (where uploadfs puts files), then SystemFS
    File file;
    if (EbookFS.exists("/reader_config.json")) {
        file = EbookFS.open("/reader_config.json", "r");
    } else if (SystemFS.exists("/reader_config.json")) {
        file = SystemFS.open("/reader_config.json", "r");
    }

    if (file) {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, file)) {
            if (doc.containsKey("refreshFrequency")) _refreshEveryNPages = doc["refreshFrequency"];
            if (doc.containsKey("fontSize")) {
                int pt = doc["fontSize"];
                _fontSizePt = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
            }
            const char* fontFamily = doc["fontFamily"] | "native";
            _useOpenSans = strcmp(fontFamily, "openSans") == 0;
        }
        file.close();
    }
    // No config file is fine - just use defaults
}

AppReader::~AppReader() {
    closeBook(false);
    if (_epubLoader) delete _epubLoader;
    if (_textRenderer) delete _textRenderer;
}

bool AppReader::hasBootResume() {
    if (!EbookFS.exists(READER_PROGRESS_PATH)) return false;

    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return false;

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    String lastBook = doc["lastBook"] | "";
    if (lastBook.length() == 0 || !doc["resumeOnBoot"]) return false;
    return EbookFS.exists(lastBook);
}

void AppReader::resumeSavedBookOnStart() {
    _resumeSavedBookOnStart = true;
}

void AppReader::start() {
    if (WiFi.getMode() != WIFI_OFF) {
        WebMgr::getInstance().stop();
        delay(50);
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        Serial.println("AppReader: WiFi powered down");
    }

    // Pick up any settings (font size, refresh interval) changed via the web UI
    // while we were away.
    loadSettings();
    if (_textRenderer) {
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_useOpenSans);
    }

    _state = VIEW_LIBRARY;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));

    if (_resumeSavedBookOnStart) {
        _resumeSavedBookOnStart = false;
        if (!openSavedProgress()) {
            markProgressInactive();
        }
    }
}

void AppReader::stop() {
    closeBook();
    InputMgr::getInstance().clearCallback();
}

const uint8_t* AppReader::getIconImage() { return icon_reader_160x160; }

void AppReader::scanBooks() {
    _books.clear();
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    File root = EbookFS.open("/");
    if(!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while(file){
        String fileName = normalizedBookName(file.name());
        String fileNameLower = fileName;
        fileNameLower.toLowerCase();
        if(fileNameLower.endsWith(".epub")) {
            BookEntry entry;
            entry.path = "/" + fileName;
            auto meta = metadata.find(fileName);
            entry.title = titleFromFilename(meta != metadata.end() ? meta->second : fileName);
            _books.push_back(entry);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}

void AppReader::drawBookTile(Book32Display& display, int x, int y, int w, int h, bool selected) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRoundRect(x, y, w, h, 5, GxEPD_BLACK);
    display.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, GxEPD_BLACK);
    display.fillRect(x + 6, y + 6, 5, h - 12, GxEPD_BLACK);

    int pageX = x + 17;
    int pageY = y + 14;
    int pageW = w - 27;
    display.drawFastHLine(pageX, pageY, pageW, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 12, pageW - 7, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 24, pageW, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 36, pageW - 11, GxEPD_BLACK);

    if (selected) {
        display.fillRect(x + w - 9, y + 8, 4, h - 16, GxEPD_BLACK);
    }
}

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    if (_state == VIEW_LIBRARY) {
        // Index -1 = "Back to Menu", 0+ = books
        int maxIndex = (int)_books.size() - 1;
        if (action == INPUT_NEXT) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = -1;  // Wrap to Back option
            _librarySelectionOnlyRedraw = _booksScanned;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex--;
            if (_selectedBookIndex < -1) _selectedBookIndex = maxIndex;  // Wrap to last book
            _librarySelectionOnlyRedraw = _booksScanned;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex == -1) {
                // Back to main menu
                markProgressInactive();
                AppMgr::getInstance().switchTo(0);
            } else if (!_books.empty() && _selectedBookIndex >= 0) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) { closeBook(); _state = VIEW_LIBRARY; _librarySelectionOnlyRedraw = false; _needsRedraw = true; }
    }
}

void AppReader::calculateTotalPages() {
    _totalBookPages = 0;
    _chapterPageCounts.clear();
    for (int i = 0; i < _epubLoader->getChapterCount(); i++) {
        std::vector<ContentNode> richContent = _epubLoader->getChapterContentRich(i);
        std::vector<String> pages;
        if(richContent.size() > 0) pages = _textRenderer->paginateRich(richContent);
        else pages = _textRenderer->paginate(_epubLoader->getChapterContent(i));
        _chapterPageCounts.push_back(pages.size());
        _totalBookPages += pages.size();
    }
}

int AppReader::getGlobalPageNumber() {
    int page = 0;
    if (_chapterPageCounts.size() > (size_t)_currentChapter) {
        for (int i = 0; i < _currentChapter; i++) page += _chapterPageCounts[i];
    }
    return page + _pageHistory.size() + 1;
}

bool AppReader::openBook(const String& path, bool restoreProgress) {
    String fullPath = "/ebooks" + path;
    closeBook(false);
    _epubLoader = new EpubLoader();
    if (!_epubLoader->open(fullPath.c_str())) { delete _epubLoader; _epubLoader = nullptr; return false; }
    _currentBookPath = path;
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _useOpenSans);
    }
    _textRenderer->setFontSize(_fontSizePt);  // Honor the current reading size
    _textRenderer->setFontFamily(_useOpenSans);

    Serial.printf("TextRenderer: Using %s fonts\n", _useOpenSans ? "Open Sans" : "native FreeSans");

    _textRenderer->calculateDimensions();

    // calculateTotalPages(); // DISABLING: This takes forever on large books
    _totalBookPages = 0; // Show simplified pagination for now
    _globalPageNumber = 1; // Start at page 1
    _currentPageRenderValid = false;
    
    int restoreChapter = 0;
    PagePointer restorePointer = {0, 0};
    int restorePage = 1;
    bool restored = restoreProgress && loadBookProgress(path, restoreChapter, restorePointer, restorePage);

    loadChapter(restored ? restoreChapter : 0);
    if (restored && restoreChapter == _currentChapter) {
        int maxNode = (int)_currentRichContent.size();
        if (restorePointer.nodeIndex >= 0 && restorePointer.nodeIndex <= maxNode && restorePointer.charOffset >= 0) {
            _currentPagePointer = restorePointer;
            _globalPageNumber = max(1, restorePage);
            _currentPageRenderValid = false;
        }
    }

    _state = VIEW_READING;
    saveReadingProgress(true);
    _needsRedraw = true;
    return true;
}

bool AppReader::openSavedProgress() {
    if (!EbookFS.exists(READER_PROGRESS_PATH)) return false;

    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return false;

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    String lastBook = doc["lastBook"] | "";
    if (lastBook.length() == 0 || !EbookFS.exists(lastBook)) return false;

    return openBook(lastBook, true);
}

bool AppReader::loadBookProgress(const String& path, int& chapter, PagePointer& pointer, int& globalPage) {
    if (!EbookFS.exists(READER_PROGRESS_PATH)) return false;

    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return false;

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    JsonObject books = doc["books"].as<JsonObject>();
    if (books.isNull() || !books.containsKey(path)) return false;

    JsonObject saved = books[path];
    chapter = saved["chapter"] | 0;
    pointer.nodeIndex = saved["nodeIndex"] | 0;
    pointer.charOffset = saved["charOffset"] | 0;
    globalPage = saved["globalPage"] | 1;
    return true;
}

void AppReader::saveReadingProgress(bool resumeOnBoot) {
    if (_currentBookPath.length() == 0 || _state != VIEW_READING) return;

    DynamicJsonDocument doc(4096);
    if (EbookFS.exists(READER_PROGRESS_PATH)) {
        File existing = EbookFS.open(READER_PROGRESS_PATH, "r");
        if (existing) {
            deserializeJson(doc, existing);
            existing.close();
        }
    }

    doc["lastBook"] = _currentBookPath;
    doc["resumeOnBoot"] = resumeOnBoot;
    JsonObject books = doc["books"].is<JsonObject>() ? doc["books"].as<JsonObject>() : doc.createNestedObject("books");
    JsonObject saved = books[_currentBookPath].is<JsonObject>() ? books[_currentBookPath].as<JsonObject>() : books.createNestedObject(_currentBookPath);
    saved["chapter"] = _currentChapter;
    saved["nodeIndex"] = _currentPagePointer.nodeIndex;
    saved["charOffset"] = _currentPagePointer.charOffset;
    saved["globalPage"] = _globalPageNumber;
    saved["updatedAt"] = millis();

    File file = EbookFS.open(READER_PROGRESS_PATH, FILE_WRITE);
    if (file) {
        serializeJson(doc, file);
        file.close();
    } else {
        Serial.println("AppReader: Failed to save reading progress");
    }
}

void AppReader::markProgressInactive() {
    if (!EbookFS.exists(READER_PROGRESS_PATH)) return;

    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return;

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return;

    doc["resumeOnBoot"] = false;
    File out = EbookFS.open(READER_PROGRESS_PATH, FILE_WRITE);
    if (out) {
        serializeJson(doc, out);
        out.close();
    }
}

void AppReader::closeBook(bool markInactive) {
    if (markInactive && _state == VIEW_READING) {
        saveReadingProgress(false);
    }
    if (_epubLoader) { _epubLoader->close(); delete _epubLoader; _epubLoader = nullptr; }
    if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
    _pageHistory.clear(); _chapterPageCounts.clear(); _totalBookPages = 0;
    _currentPageRenderValid = false;
}

void AppReader::loadChapter(int chapterIndex) {
    if (!_epubLoader) return;
    if (chapterIndex < 0 || chapterIndex >= _epubLoader->getChapterCount()) return;
    
    int originalIndex = chapterIndex;
    while (chapterIndex < _epubLoader->getChapterCount()) {
        _currentChapter = chapterIndex;
        _pageHistory.clear();
        _currentPagePointer = {0, 0};
        _currentPageRenderValid = false;
        
        _currentRichContent = _epubLoader->getChapterContentRich(chapterIndex);
        if (_currentRichContent.size() > 0) {
            if (_textRenderer) _textRenderer->clearCache();
            _needsRedraw = true;
            return;
        }
        chapterIndex++;
    }
    _currentChapter = originalIndex;
    _currentPageRenderValid = false;
    if (_textRenderer) _textRenderer->clearCache();
    _needsRedraw = true;
}

void AppReader::nextPage() {
    if (!_textRenderer) return;

    RenderResult result = _currentPageRender;
    if (!_currentPageRenderValid) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        int currentPageNum = _pageHistory.size();
        result = _textRenderer->renderRichPageDynamic(display, _currentRichContent,
                                                      _currentPagePointer.nodeIndex,
                                                      _currentPagePointer.charOffset,
                                                      currentPageNum, 0, false);
    }
    
    if (result.pageFull) {
        // Save current position to history before advancing
        _pageHistory.push_back(_currentPagePointer);
        
        // Continue from the exact node/character where rendering stopped.
        _currentPagePointer.nodeIndex = result.nextNodeIndex;
        _currentPagePointer.charOffset = result.nextCharOffset;
        
        // Increment global page counter
        _globalPageNumber++;
        
        // Clear cache since we're moving to a new page
        _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // End of chapter - advance to next
        if (_currentChapter < _epubLoader->getChapterCount() - 1) {
            // Save current chapter state to history
            _pageHistory.push_back(_currentPagePointer);
            _globalPageNumber++; // Next page in next chapter
            loadChapter(_currentChapter + 1);
            saveReadingProgress(true);
        }
        // If at end of book, do nothing
    }
}

void AppReader::prevPage() {
    if (!_pageHistory.empty()) {
        _currentPagePointer = _pageHistory.back();
        _pageHistory.pop_back();
        if (_globalPageNumber > 1) _globalPageNumber--; // Decrement global page counter
        if (_textRenderer) _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // Go to previous chapter
        if (_currentChapter > 0) {
            // NOTE: Going to the "last page" of the previous chapter is tricky
            // because we don't know where it starts without rendering it.
            // For now, we go to the start of the previous chapter.
            if (_globalPageNumber > 1) _globalPageNumber--; // Decrement for prev chapter
            _currentPageRenderValid = false;
            prevChapter();
            saveReadingProgress(true);
        }
    }
}

void AppReader::nextChapter() {
    if (!_epubLoader) return;
    if (_currentChapter < _epubLoader->getChapterCount() - 1) loadChapter(_currentChapter + 1);
}

void AppReader::prevChapter() {
    if (!_epubLoader) return;
    if (_currentChapter > 0) {
        int tryChapter = _currentChapter - 1;
        while (tryChapter >= 0) {
            String chapterText = _epubLoader->getChapterContent(tryChapter);
            if (chapterText.length() > 0) { loadChapter(tryChapter); return; }
            tryChapter--;
        }
    }
}

void AppReader::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    if (_state == VIEW_LIBRARY) drawLibrary();
    else drawReading();
}

void AppReader::drawLibrary() {
    if (!_booksScanned) { scanBooks(); _booksScanned = true; }
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 110;
    const int ITEM_PADDING = 24;

    // Use Partial Refresh for Library interactions
    if (_librarySelectionOnlyRedraw) {
        LibraryDirtyRect dirty = unionLibraryRect(libraryItemRect(_previousBookIndex, display.width()),
                                                 libraryItemRect(_selectedBookIndex, display.width()));
        LibraryDirtyRect footer = {18, display.height() - 48, display.width() - 36, 46};
        dirty = unionLibraryRect(dirty, footer);
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > display.width()) dirty.w = display.width() - dirty.x;
        if (dirty.y + dirty.h > display.height()) dirty.h = display.height() - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    _librarySelectionOnlyRedraw = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawTextWithFont(display, "Library", 20, 40, &FreeSansBold12pt7b, GxEPD_BLACK);
        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 20, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        display.drawFastHLine(20, 56, display.width() - 40, GxEPD_BLACK);
        display.drawFastHLine(20, 58, 72, GxEPD_BLACK);

        int y = HEADER_H;

        // === "Back to Menu" option (index -1) ===
        bool backSelected = (_selectedBookIndex == -1);
        if (backSelected) {
            display.fillRect(20, y + 6, 5, BACK_ITEM_HEIGHT - 12, GxEPD_BLACK);
            display.drawRoundRect(16, y + 2, display.width() - 32, BACK_ITEM_HEIGHT - 4, 6, GxEPD_BLACK);
        }
        drawTextWithFont(display, "<  Back to Menu", ITEM_PADDING + 14, y + 32,
                         backSelected ? &FreeSansBold12pt7b : &FreeSans12pt7b, GxEPD_BLACK);
        display.drawFastHLine(ITEM_PADDING, y + BACK_ITEM_HEIGHT - 1, display.width() - (ITEM_PADDING * 2), GxEPD_BLACK);
        y += BACK_ITEM_HEIGHT;

        // === Book list ===
        if (_books.empty()) {
            drawTextWithFont(display, "No books found.", 28, y + 54, &FreeSansBold12pt7b, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload EPUBs via web.", 28, y + 88, FONT_SIZE_BODY, GxEPD_BLACK);
        } else {
            int idx = 0;
            for (const auto& book : _books) {
                if (y > display.height() - 70) break;

                bool isSelected = (idx == _selectedBookIndex);
                if (isSelected) {
                    display.fillRect(20, y + 12, 5, ITEM_HEIGHT - 24, GxEPD_BLACK);
                    display.drawRoundRect(16, y + 4, display.width() - 32, ITEM_HEIGHT - 8, 6, GxEPD_BLACK);
                } else {
                    display.drawFastHLine(ITEM_PADDING, y + ITEM_HEIGHT - 1, display.width() - (ITEM_PADDING * 2), GxEPD_BLACK);
                }

                int coverW = COVER_WIDTH;
                int coverH = COVER_HEIGHT;
                int coverX = ITEM_PADDING + 12;
                int coverY = y + (ITEM_HEIGHT - coverH) / 2;
                drawBookTile(display, coverX, coverY, coverW, coverH, isSelected);
                if (isSelected) {
                    display.drawRect(coverX - 3, coverY - 3, coverW + 6, coverH + 6, GxEPD_BLACK);
                    display.drawRect(coverX - 2, coverY - 2, coverW + 4, coverH + 4, GxEPD_BLACK);
                }

                uint16_t textColor = GxEPD_BLACK;

                // Draw book title with word wrapping
                String title = book.title;
                const GFXfont* titleFont = isSelected ? &FreeSansBold12pt7b : &FreeSans12pt7b;
                int textX = ITEM_PADDING + COVER_WIDTH + 44;
                int textY = y + (isSelected ? 36 : 34);
                int lineCount = 0;
                const int MAX_LINES = isSelected ? 3 : 2;
                const int LINE_HEIGHT = isSelected ? 27 : 25;
                const int MAX_WIDTH = display.width() - textX - 28;

                int pos = 0;
                while (pos < (int)title.length() && lineCount < MAX_LINES) {
                    String line = "";
                    while (pos < (int)title.length()) {
                        int nextSpace = title.indexOf(' ', pos);
                        if (nextSpace == -1) nextSpace = title.length();
                        String word = title.substring(pos, nextSpace);
                        String testLine = line.length() > 0 ? line + " " + word : word;
                        if (textWidthForFont(display, testLine.c_str(), titleFont) > MAX_WIDTH && line.length() > 0) break;
                        line = testLine;
                        pos = nextSpace + 1;
                    }
                    if (lineCount == MAX_LINES - 1 && pos < (int)title.length() && line.length() > 3) {
                        line = line.substring(0, line.length() - 3) + "...";
                    }
                    drawTextWithFont(display, line.c_str(), textX, textY, titleFont, textColor);
                    textY += LINE_HEIGHT;
                    lineCount++;
                }

                y += ITEM_HEIGHT;
                idx++;
            }
        }

        // Page indicator (14px) - show current selection
        char pageStr[24];
        if (_selectedBookIndex == -1) {
            snprintf(pageStr, sizeof(pageStr), "Menu");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }
        display.drawFastHLine(20, display.height() - 42, display.width() - 40, GxEPD_BLACK);
        fontMgr.drawText(display, "Next: Move  |  Hold: Open", 22, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);
        fontMgr.drawTextRight(display, pageStr, display.width() - 20, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}

void AppReader::drawReading() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    
    // Check if we need a full refresh
    if (_readingFirstDraw || _pageTurnsSinceRefresh >= _refreshEveryNPages) {
        Serial.println("AppReader: Full Refresh Cycle");
        display.setFullWindow();
        _pageTurnsSinceRefresh = 0;
        _readingFirstDraw = false;
    }
    else { 
        Serial.printf("AppReader: Partial Refresh (%d/%d)\n", _pageTurnsSinceRefresh + 1, _refreshEveryNPages);
        display.setPartialWindow(0, 0, display.width(), display.height()); 
        _pageTurnsSinceRefresh++; 
    }
    
    // Page numbers: use _globalPageNumber which is tracked at runtime
    int currentPageNum = _pageHistory.size();  // For render cache key
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        _currentPageRender = _textRenderer->renderRichPageDynamic(display, _currentRichContent,
                                                                _currentPagePointer.nodeIndex,
                                                                _currentPagePointer.charOffset,
                                                                currentPageNum, _globalPageNumber, true);
        _currentPageRenderValid = true;
        // Draw page number directly here for consistent display
        display.setFont(NULL);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(display.width()/2 - 20, display.height() - 15);
        display.printf("Page %d", _globalPageNumber);
    } while (display.nextPage());
}

void AppReader::update() {
    // Library rendering is static unless input changes selection.
}

void AppReader::applyFontSize(int pt) {
    int normalized = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
    _fontSizePt = normalized;
    if (_textRenderer) _textRenderer->setFontSize(normalized);

    // Re-render the current page from its saved start pointer at the new size.
    // The pointer is a content position (node + char offset), so it's font-size
    // independent; the renderer recomputes where this page ends and the next
    // begins, keeping word-wrap and page breaks consistent.
    _currentPageRenderValid = false;
    _readingFirstDraw = true;     // Full refresh to clear the old layout cleanly
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;
}

void AppReader::applyFontFamily(bool useOpenSans) {
    _useOpenSans = useOpenSans;
    if (_textRenderer) _textRenderer->setFontFamily(useOpenSans);
    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;
}

void AppReader::forceRedraw() {
    _librarySelectionOnlyRedraw = false;  // Repaint the whole library view
    _currentPageRenderValid = false;
    _readingFirstDraw = true;             // Repaint the whole reading view
    _needsRedraw = true;
}
