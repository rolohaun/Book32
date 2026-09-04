#include "AppReader.h"
#include "SoundMgr.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "BatteryMgr.h"
#include "icon_reader.h"
#include "Book32FS.h"
#include "WebMgr.h"
#include "Config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <map>
#include <utility>

static const char* READER_PROGRESS_PATH = "/reader_progress.json";
static const uint32_t READER_LAYOUT_VERSION = 5;

static uint32_t fingerprintBook(const String& path, uint32_t& sizeOut) {
    sizeOut = 0;
    File file = EbookFS.open(path, FILE_READ);
    if (!file) return 0;

    sizeOut = static_cast<uint32_t>(file.size());
    uint32_t hash = 2166136261u;
    auto mix = [&hash](uint8_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    for (int shift = 0; shift < 32; shift += 8) mix(static_cast<uint8_t>((sizeOut >> shift) & 0xFF));

    uint8_t buffer[256];
    size_t read = file.read(buffer, sizeof(buffer));
    for (size_t i = 0; i < read; i++) mix(buffer[i]);

    if (sizeOut > sizeof(buffer)) {
        file.seek(sizeOut - sizeof(buffer));
        read = file.read(buffer, sizeof(buffer));
        for (size_t i = 0; i < read; i++) mix(buffer[i]);
    }
    file.close();
    return hash;
}

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

static int nextUtf8Boundary(const String& text, int offset) {
    if (offset >= static_cast<int>(text.length())) return text.length();
    int next = offset + 1;
    while (next < static_cast<int>(text.length()) &&
           (static_cast<uint8_t>(text[next]) & 0xC0) == 0x80) next++;
    return next;
}

static void removeLastUtf8Codepoint(String& text) {
    if (text.length() == 0) return;
    int start = text.length() - 1;
    while (start > 0 && (static_cast<uint8_t>(text[start]) & 0xC0) == 0x80) start--;
    text.remove(start);
}

static int fittingPrefix(Book32Display& display, const String& text, const GFXfont* font, int maxWidth) {
    int offset = 0;
    int fitted = 0;
    while (offset < static_cast<int>(text.length())) {
        int next = nextUtf8Boundary(text, offset);
        if (textWidthForFont(display, text.substring(0, next).c_str(), font) > maxWidth) break;
        fitted = next;
        offset = next;
    }
    return fitted;
}

static std::vector<String> wrapLibraryTitle(Book32Display& display, String title, const GFXfont* font,
                                            int maxWidth, int maxLines) {
    std::vector<String> lines;
    lines.reserve(maxLines);
    title.trim();
    String line;
    int position = 0;
    bool truncated = false;

    while (position < static_cast<int>(title.length())) {
        while (position < static_cast<int>(title.length()) &&
               isspace(static_cast<unsigned char>(title[position]))) position++;
        if (position >= static_cast<int>(title.length())) break;

        int wordEnd = position;
        while (wordEnd < static_cast<int>(title.length()) &&
               !isspace(static_cast<unsigned char>(title[wordEnd]))) wordEnd++;
        String word = title.substring(position, wordEnd);
        position = wordEnd;

        String candidate = line.length() > 0 ? line + " " + word : word;
        if (textWidthForFont(display, candidate.c_str(), font) <= maxWidth) {
            line = candidate;
            continue;
        }

        if (line.length() > 0) {
            lines.push_back(line);
            line = "";
            if (static_cast<int>(lines.size()) >= maxLines) {
                truncated = true;
                break;
            }
        }

        while (word.length() > 0) {
            int fitted = fittingPrefix(display, word, font, maxWidth);
            if (fitted <= 0) fitted = nextUtf8Boundary(word, 0);
            line = word.substring(0, fitted);
            word.remove(0, fitted);
            if (word.length() == 0) break;

            lines.push_back(line);
            line = "";
            if (static_cast<int>(lines.size()) >= maxLines) {
                truncated = true;
                break;
            }
        }
        if (truncated) break;
    }

    if (!truncated && line.length() > 0) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(maxLines);
        truncated = true;
    }
    if (position < static_cast<int>(title.length())) truncated = true;

    if (truncated) {
        String& last = lines.back();
        last.trim();
        while (last.length() > 0 && textWidthForFont(display, (last + "...").c_str(), font) > maxWidth) {
            removeLastUtf8Codepoint(last);
            last.trim();
        }
        last += "...";
    }
    return lines;
}

static String titleFromFilename(String name) {
    String lower = name;
    lower.toLowerCase();
    if (lower.endsWith(".epub")) name.remove(name.length() - 5);
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

static void saveBookMetadata(const std::map<String, String>& metadata) {
    DynamicJsonDocument doc(4096);
    for (const auto& item : metadata) doc[item.first] = item.second;

    File file = SystemFS.open("/books_meta.json", FILE_WRITE);
    if (!file) return;
    serializeJson(doc, file);
    file.close();
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
    _libraryFirstDraw = true;
    _librarySelectionOnlyRedraw = false;
    _resumeSavedBookOnStart = false;
    _previousBookIndex = 0;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _currentChapter = 0;
    _currentBookSize = 0;
    _currentBookFingerprint = 0;
    _currentPage = 0;
    _needsRedraw = true;
    _currentPageRender = {0, 0, false, 0, 0};
    _currentPageRenderValid = false;
    _pageTurnsSinceRefresh = 0;
    _totalBookPages = 0;
    _refreshEveryNPages = READER_FULL_REFRESH_INTERVAL_DEFAULT;
    _fontSizePt = READER_FONT_SIZE_DEFAULT;
    _useOpenSans = false;     // Preserve the original reader font by default
    _showChapter = true;
    _showPageNumber = true;
    _showReadingPercentage = true;
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
            _showChapter = doc["showChapter"] | true;
            _showPageNumber = doc["showPageNumber"] | true;
            _showReadingPercentage = doc["showReadingPercentage"] | true;
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
    BatteryMgr::getInstance().setReaderActive(true);

    // Pick up any settings (font size, refresh interval) changed via the web UI
    // while we were away.
    loadSettings();
    if (_textRenderer) {
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_useOpenSans);
    }

    _state = VIEW_LIBRARY;
    _booksScanned = false;
    _selectedBookIndex = 0;
    _previousBookIndex = 0;
    _scrollOffset = 0;
    _libraryFirstDraw = true;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppReader::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));

    if (_resumeSavedBookOnStart) {
        _resumeSavedBookOnStart = false;
        if (!openSavedProgress()) {
            markProgressInactive();
        }
    }
}

void AppReader::stop() {
    BatteryMgr::getInstance().setReaderActive(false);
    closeBook();
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

const uint8_t* AppReader::getIconImage() { return icon_reader_160x160; }

void AppReader::scanBooks() {
    // Keep decoded covers for books that are still present. AppReader stays
    // alive while switching apps, so reopening the library should not decode
    // the same cover from the SD card again.
    std::vector<BookEntry> previousBooks = std::move(_books);
    _books.clear();
    std::map<String, String> metadata;
    loadBookMetadata(metadata);
    bool metadataChanged = false;

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
            for (const BookEntry& previous : previousBooks) {
                if (previous.path == entry.path) {
                    entry.coverAttempted = previous.coverAttempted;
                    entry.cover = previous.cover;
                    break;
                }
            }
            auto meta = metadata.find(fileName);
            String displayTitle = meta != metadata.end() ? meta->second : "";
            if (displayTitle.length() == 0) {
                EpubLoader loader;
                String fullPath = "/ebooks/" + fileName;
                if (loader.open(fullPath.c_str(), false)) {
                    displayTitle = loader.getTitle();
                    displayTitle.trim();
                    loader.close();
                }
                if (displayTitle.length() > 0) {
                    metadata[fileName] = displayTitle;
                    metadataChanged = true;
                }
            }
            entry.title = titleFromFilename(displayTitle.length() > 0 ? displayTitle : fileName);
            _books.push_back(entry);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    if (_books.empty()) {
        // With nothing to open, make the center button's only useful action Back.
        _selectedBookIndex = -1;
    } else if (_selectedBookIndex < 0 || _selectedBookIndex >= static_cast<int>(_books.size())) {
        _selectedBookIndex = 0;
    }
    _previousBookIndex = _selectedBookIndex;
    if (metadataChanged) saveBookMetadata(metadata);
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

void AppReader::loadBookCover(BookEntry& book, int width, int height) {
    if (book.coverAttempted) return;
    book.coverAttempted = true;

    EpubLoader loader;
    String fullPath = "/ebooks" + book.path;
    if (!loader.open(fullPath.c_str(), false)) return;
    String coverHref = loader.getCoverHref();
    if (coverHref.length() > 0) {
        std::shared_ptr<EpubBitmap> cover(new (std::nothrow) EpubBitmap());
        if (cover && loader.decodeImage(coverHref, width, height, *cover)) book.cover = cover;
    }
    loader.close();
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
            } else if (_selectedBookIndex >= 0 && _selectedBookIndex < static_cast<int>(_books.size())) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) {
            closeBook();
            _state = VIEW_LIBRARY;
            _libraryFirstDraw = true;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
    }
}

void AppReader::handleTouch(uint16_t x, uint16_t y) {
    if (_state == VIEW_READING) {
        // Invisible page-turn zones cover the outer thirds of the page.
        if (x < SCREEN_WIDTH / 3) prevPage();
        else if (x >= (SCREEN_WIDTH * 2) / 3) nextPage();
        return;
    }

    constexpr int HEADER_H = 76;
    constexpr int BACK_ITEM_HEIGHT = 48;
    constexpr int ITEM_HEIGHT = 110;
    constexpr int FOOTER_H = 70;
    if (y >= HEADER_H && y < HEADER_H + BACK_ITEM_HEIGHT) {
        SoundMgr::getInstance().beep();
        markProgressInactive();
        AppMgr::getInstance().switchTo(0);
        return;
    }
    if (y < HEADER_H + BACK_ITEM_HEIGHT || y >= SCREEN_HEIGHT - FOOTER_H) return;

    int visibleRow = (static_cast<int>(y) - HEADER_H - BACK_ITEM_HEIGHT) / ITEM_HEIGHT;
    int bookIndex = _scrollOffset + visibleRow;
    if (bookIndex >= 0 && bookIndex < static_cast<int>(_books.size())) {
        SoundMgr::getInstance().beep();
        _selectedBookIndex = bookIndex;
        openBook(_books[bookIndex].path);
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
    drawLoadingScreen("Opening book", "Indexing chapters...", 15, _libraryFirstDraw);
    closeBook(false);
    _currentBookPath = path;
    _currentBookFingerprint = fingerprintBook(path, _currentBookSize);
    _epubLoader = new EpubLoader();
    if (!_epubLoader->open(fullPath.c_str())) {
        delete _epubLoader;
        _epubLoader = nullptr;
        _currentBookPath = "";
        _currentBookSize = 0;
        _currentBookFingerprint = 0;
        return false;
    }
    drawLoadingScreen("Opening book", "Loading book content...", 70, false);
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _useOpenSans);
    }
    _textRenderer->setImageSource(_epubLoader);
    _textRenderer->setFontSize(_fontSizePt);  // Honor the current reading size
    _textRenderer->setFontFamily(_useOpenSans);

    Serial.printf("TextRenderer: Using %s fonts\n", _useOpenSans ? "Open Sans" : "native FreeSans");

    _textRenderer->calculateDimensions();
    resetPageCacheForLayout();

    // calculateTotalPages(); // DISABLING: This takes forever on large books
    _totalBookPages = 0; // Show simplified pagination for now
    _globalPageNumber = 1; // Start at page 1
    _currentPageRenderValid = false;
    
    int restoreChapter = 0;
    PagePointer restorePointer = {0, 0};
    int restorePage = 1;
    uint32_t restoreVisibleOffset = 0;
    bool hasVisibleOffset = false;
    bool restored = restoreProgress && loadBookProgress(path, restoreChapter, restorePointer, restorePage,
                                                        restoreVisibleOffset, hasVisibleOffset);

    drawLoadingScreen("Opening book", restored ? "Restoring your page..." : "Laying out first page...", 88, false);
    loadChapter(restored ? restoreChapter : 0);
    if (restored && restoreChapter == _currentChapter) {
        if (hasVisibleOffset) {
            _currentPagePointer = ReaderPosition::fromVisibleOffset(_currentRichContent, restoreVisibleOffset);
            _globalPageNumber = max(1, restorePage);
            _currentPageRenderValid = false;
        } else {
            int maxNode = (int)_currentRichContent.size();
            if (restorePointer.nodeIndex >= 0 && restorePointer.nodeIndex <= maxNode && restorePointer.charOffset >= 0) {
                _currentPagePointer = restorePointer;
                _globalPageNumber = max(1, restorePage);
                _currentPageRenderValid = false;
            }
        }
        rebuildPageHistory();
        rememberCurrentPage();
    }

    _state = VIEW_READING;
    _readingFirstDraw = true;
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

bool AppReader::loadBookProgress(const String& path, int& chapter, PagePointer& pointer, int& globalPage,
                                 uint32_t& visibleOffset, bool& hasVisibleOffset) {
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
    if (saved.containsKey("bookFingerprint") && saved["bookFingerprint"].as<uint32_t>() != _currentBookFingerprint) {
        return false;
    }
    chapter = saved["chapter"] | 0;
    pointer.nodeIndex = saved["nodeIndex"] | 0;
    pointer.charOffset = saved["charOffset"] | 0;
    globalPage = saved["globalPage"] | 1;
    hasVisibleOffset = saved.containsKey("visibleOffset") && (saved["positionVersion"] | 0) >= 3;
    visibleOffset = saved["visibleOffset"] | 0;
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
    saved["visibleOffset"] = currentVisibleOffset();
    saved["positionVersion"] = 3;
    saved["bookFingerprint"] = _currentBookFingerprint;
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
    _currentRichContent.clear();
    _currentBookPath = "";
    _currentBookSize = 0;
    _currentBookFingerprint = 0;
    _pageCache.clearMemory();
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
            rememberCurrentPage();
            rebuildPageHistory();
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
        rememberCurrentPage();
        _pageHistory.push_back(_currentPagePointer);
        
        // Continue from the exact node/character where rendering stopped.
        _currentPagePointer.nodeIndex = result.nextNodeIndex;
        _currentPagePointer.charOffset = result.nextCharOffset;
        rememberCurrentPage();
        
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
            rememberCurrentPage();
            _pageCache.markChapterComplete(static_cast<uint16_t>(_currentChapter));
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
            std::vector<ContentNode> content = _epubLoader->getChapterContentRich(tryChapter);
            if (!content.empty()) {
                _currentChapter = tryChapter;
                _currentRichContent = std::move(content);
                _currentPagePointer = {0, 0};
                _pageHistory.clear();
                if (_textRenderer) _textRenderer->clearCache();

                if (_pageCache.isChapterComplete(static_cast<uint16_t>(tryChapter))) {
                    std::vector<uint32_t> pages = _pageCache.pagesForChapter(static_cast<uint16_t>(tryChapter));
                    if (!pages.empty()) {
                        _currentPagePointer = ReaderPosition::fromVisibleOffset(_currentRichContent, pages.back());
                    }
                }
                rebuildPageHistory();
                rememberCurrentPage();
                _currentPageRenderValid = false;
                _needsRedraw = true;
                return;
            }
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

void AppReader::drawLoadingScreen(const char* title, const char* status, uint8_t progress, bool fullRefresh) {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    const int barWidth = display.width() - 100;
    const int barHeight = 28;
    const int barX = 50;
    const int barY = display.height() / 2;
    const int fillWidth = ((barWidth - 4) * constrain(progress, 0, 100)) / 100;

    if (fullRefresh) display.setFullWindow();
    else display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        fontMgr.drawTextCentered(display, title, barY - 72, FONT_SIZE_TITLE, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, status, barY - 32, FONT_SIZE_BODY, GxEPD_BLACK);
        display.drawRoundRect(barX, barY, barWidth, barHeight, 7, GxEPD_BLACK);
        display.drawRoundRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, 5, GxEPD_BLACK);
        if (fillWidth > 0) display.fillRoundRect(barX + 2, barY + 2, fillWidth, barHeight - 4, 5, GxEPD_BLACK);
        char percentText[8];
        snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(progress));
        fontMgr.drawTextCentered(display, percentText, barY + 72, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
    } while (display.nextPage());
}

void AppReader::drawLibrary() {
    const bool initialLoad = !_booksScanned;
    if (initialLoad) {
        drawLoadingScreen("Opening eReader", "Finding books...", 15, true);
        _libraryFirstDraw = false;  // The loading screen already established a clean full-refresh baseline.
        scanBooks();
        _booksScanned = true;
    }
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 110;
    const int ITEM_PADDING = 24;
    const int visibleBookCount = max(1, (display.height() - HEADER_H - BACK_ITEM_HEIGHT - 70) / ITEM_HEIGHT);

    int previousScrollOffset = _scrollOffset;
    if (_selectedBookIndex >= 0) {
        if (_selectedBookIndex < _scrollOffset) _scrollOffset = _selectedBookIndex;
        if (_selectedBookIndex >= _scrollOffset + visibleBookCount) {
            _scrollOffset = _selectedBookIndex - visibleBookCount + 1;
        }
    }
    int maximumScroll = max(0, static_cast<int>(_books.size()) - visibleBookCount);
    _scrollOffset = constrain(_scrollOffset, 0, maximumScroll);
    if (_scrollOffset != previousScrollOffset) _librarySelectionOnlyRedraw = false;

    int lastVisibleBook = min(static_cast<int>(_books.size()), _scrollOffset + visibleBookCount);
    bool coversNeedLoading = false;
    for (int index = _scrollOffset; index < lastVisibleBook; index++) {
        if (!_books[index].coverAttempted) {
            coversNeedLoading = true;
            break;
        }
    }
    if (initialLoad && coversNeedLoading) {
        drawLoadingScreen("Opening eReader", "Loading book covers...", 60, false);
    }
    for (int index = _scrollOffset; index < lastVisibleBook; index++) {
        loadBookCover(_books[index], COVER_WIDTH, COVER_HEIGHT);
    }

    // Entering the library replaces an unrelated screen, so it needs the full
    // waveform. Partial refresh is reserved for subsequent selection changes.
    if (_libraryFirstDraw) {
        display.setFullWindow();
        _libraryFirstDraw = false;
    } else if (_librarySelectionOnlyRedraw) {
        int previousVisibleIndex = _previousBookIndex < 0 ? -1 : _previousBookIndex - _scrollOffset;
        int selectedVisibleIndex = _selectedBookIndex < 0 ? -1 : _selectedBookIndex - _scrollOffset;
        LibraryDirtyRect dirty = unionLibraryRect(libraryItemRect(previousVisibleIndex, display.width()),
                                                 libraryItemRect(selectedVisibleIndex, display.width()));
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
            int idx = _scrollOffset;
            for (; idx < lastVisibleBook; idx++) {
                if (y > display.height() - 70) break;
                const BookEntry& book = _books[idx];

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
                if (book.cover && book.cover->valid()) {
                    display.fillRect(coverX, coverY, coverW, coverH, GxEPD_WHITE);
                    int bitmapX = coverX + (coverW - book.cover->width) / 2;
                    int bitmapY = coverY + (coverH - book.cover->height) / 2;
                    book.cover->draw(display, bitmapX, bitmapY);
                    display.drawRect(coverX, coverY, coverW, coverH, GxEPD_BLACK);
                } else {
                    drawBookTile(display, coverX, coverY, coverW, coverH, isSelected);
                }
                if (isSelected) {
                    display.drawRect(coverX - 3, coverY - 3, coverW + 6, coverH + 6, GxEPD_BLACK);
                    display.drawRect(coverX - 2, coverY - 2, coverW + 4, coverH + 4, GxEPD_BLACK);
                }

                uint16_t textColor = GxEPD_BLACK;

                const GFXfont* titleFont = isSelected ? &FreeSansBold12pt7b : &FreeSans12pt7b;
                int textX = ITEM_PADDING + COVER_WIDTH + 44;
                const int MAX_WIDTH = display.width() - textX - 28;
                const int MAX_LINES = 3;
                std::vector<String> titleLines = wrapLibraryTitle(display, book.title, titleFont,
                                                                  MAX_WIDTH, MAX_LINES);
                int lineHeight = titleFont->yAdvance;
                int16_t boundsX, boundsY;
                uint16_t boundsWidth, boundsHeight;
                display.setFont(titleFont);
                display.getTextBounds("Ag", 0, 0, &boundsX, &boundsY, &boundsWidth, &boundsHeight);
                int totalHeight = boundsHeight + (static_cast<int>(titleLines.size()) - 1) * lineHeight;
                int textY = y + ((ITEM_HEIGHT - totalHeight) / 2) - boundsY;
                for (const String& line : titleLines) {
                    drawTextWithFont(display, line.c_str(), textX, textY, titleFont, textColor);
                    textY += lineHeight;
                }

                y += ITEM_HEIGHT;
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
#if BOOK32_HAS_TOUCH
        fontMgr.drawText(display, "Tap a book to open", 22, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);
#else
        fontMgr.drawText(display, "Next: Move  |  Hold: Open", 22, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);
#endif
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
    const uint32_t chapterLength = _showReadingPercentage
                                       ? ReaderPosition::totalVisibleLength(_currentRichContent)
                                       : 0;
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        _currentPageRender = _textRenderer->renderRichPageDynamic(display, _currentRichContent,
                                                                _currentPagePointer.nodeIndex,
                                                                _currentPagePointer.charOffset,
                                                                currentPageNum, _globalPageNumber, true);
        _currentPageRenderValid = true;
        // The footer is deliberately drawn outside TextRenderer so its
        // visibility never affects pagination or the page cache.
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);

        const int footerY = display.height() - 24;
        if (_showChapter) {
            String chapterTitle = _epubLoader ? _epubLoader->getChapterTitle(_currentChapter) : "";
            if (chapterTitle.length() == 0) chapterTitle = "Chapter " + String(_currentChapter + 1);
            while (chapterTitle.length() > 0) {
                int16_t textX, textY;
                uint16_t textW, textH;
                display.getTextBounds(chapterTitle, 0, 0, &textX, &textY, &textW, &textH);
                if (textW <= display.width() * 0.40f) break;
                chapterTitle.remove(chapterTitle.length() - 1);
            }
            display.setCursor(12, footerY);
            display.print(chapterTitle);
        }
        if (_showPageNumber) {
            char pageText[24];
            snprintf(pageText, sizeof(pageText), "Page %d", _globalPageNumber);
            int16_t textX, textY;
            uint16_t textW, textH;
            display.getTextBounds(pageText, 0, 0, &textX, &textY, &textW, &textH);
            display.setCursor((display.width() - textW) / 2, footerY);
            display.print(pageText);
        }
        if (_showReadingPercentage) {
            PagePointer pageEnd = {_currentPageRender.nextNodeIndex, _currentPageRender.nextCharOffset};
            uint32_t chapterRead = _currentPageRender.pageFull
                                       ? ReaderPosition::toVisibleOffset(_currentRichContent, pageEnd)
                                       : chapterLength;
            float chapterFraction = chapterLength > 0
                                        ? min(1.0f, chapterRead / static_cast<float>(chapterLength))
                                        : 0.0f;
            const float bookProgress = _epubLoader
                                           ? _epubLoader->calculateBookProgress(_currentChapter, chapterFraction)
                                           : 0.0f;
            int percentage = constrain(static_cast<int>(bookProgress * 100.0f + 0.5f), 0, 100);
            char percentageText[8];
            snprintf(percentageText, sizeof(percentageText), "%d%%", percentage);
            int16_t textX, textY;
            uint16_t textW, textH;
            display.getTextBounds(percentageText, 0, 0, &textX, &textY, &textW, &textH);
            display.setCursor(display.width() - textW - 12, footerY);
            display.print(percentageText);
        }
        display.setTextSize(1);
    } while (display.nextPage());
}

void AppReader::update() {
    // Library rendering is static unless input changes selection.
}

void AppReader::applyFontSize(int pt) {
    int normalized = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
    _fontSizePt = normalized;
    if (_textRenderer) _textRenderer->setFontSize(normalized);
    if (_epubLoader) resetPageCacheForLayout();

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
    if (_epubLoader) resetPageCacheForLayout();
    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;
}

void AppReader::forceRedraw() {
    _libraryFirstDraw = true;
    _librarySelectionOnlyRedraw = false;  // Repaint the whole library view
    _currentPageRenderValid = false;
    _readingFirstDraw = true;             // Repaint the whole reading view
    _needsRedraw = true;
}

uint32_t AppReader::currentVisibleOffset() const {
    return ReaderPosition::toVisibleOffset(_currentRichContent, _currentPagePointer);
}

uint32_t AppReader::readerLayoutKey() const {
    const Book32Display& display = DisplayMgr::getInstance().getDisplay();
    uint32_t hash = 2166136261u;
    const uint32_t values[] = {
        READER_LAYOUT_VERSION,
        static_cast<uint32_t>(_fontSizePt),
        static_cast<uint32_t>(_useOpenSans ? 1 : 0),
        static_cast<uint32_t>(display.width()),
        static_cast<uint32_t>(display.height())
    };
    for (uint32_t value : values) {
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint8_t>((value >> shift) & 0xFF);
            hash *= 16777619u;
        }
    }
    return hash;
}

void AppReader::resetPageCacheForLayout() {
    if (_currentBookPath.length() == 0) return;
    _pageCache.begin(_currentBookPath, _currentBookSize, _currentBookFingerprint, readerLayoutKey());
    _pageHistory.clear();
    if (!_currentRichContent.empty()) rememberCurrentPage();
}

void AppReader::rememberCurrentPage() {
    if (_currentRichContent.empty() || _currentChapter < 0) return;
    _pageCache.rememberPage(static_cast<uint16_t>(_currentChapter), currentVisibleOffset());
}

void AppReader::rebuildPageHistory() {
    _pageHistory.clear();
    if (_currentRichContent.empty() || _currentChapter < 0) return;

    uint32_t currentOffset = currentVisibleOffset();
    std::vector<uint32_t> pages = _pageCache.pagesForChapter(static_cast<uint16_t>(_currentChapter));
    for (uint32_t offset : pages) {
        if (offset >= currentOffset) break;
        _pageHistory.push_back(ReaderPosition::fromVisibleOffset(_currentRichContent, offset));
    }
}
