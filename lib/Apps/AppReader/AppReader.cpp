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
#include "AppSelection.h"
#include "GitHubMgr.h"
#include "HomeServices.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <map>
#include <utility>

static const char* READER_PROGRESS_PATH = "/reader_progress.json";
static const uint32_t READER_LAYOUT_VERSION = 6;
static const unsigned long PROGRESS_SAVE_DELAY_MS = 2000;

// The library is a bookcase: three shelves of three books, each standing on
// its plank with its title underneath. Covers are decoded (and cached) at
// up to this size; the LILYGO's taller screen takes larger ones.
#if defined(BOARD_LILYGO_T5S3_PRO)
static const int LIBRARY_COVER_WIDTH = 120;
static const int LIBRARY_COVER_HEIGHT = 160;
#else
static const int LIBRARY_COVER_WIDTH = 104;
static const int LIBRARY_COVER_HEIGHT = 138;
#endif

static const char* COVER_CACHE_DIR = "/.covers";
static const uint32_t COVER_CACHE_MAGIC = 0x43323342;  // B32C
// Bump when the cover decoding pipeline changes so old thumbnails re-render.
static const uint16_t COVER_CACHE_VERSION = 1;

struct __attribute__((packed)) CoverCacheHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t maxWidth;
    uint16_t maxHeight;
    uint16_t width;   // 0 when the book has no decodable cover
    uint16_t height;
    uint32_t bookSize;
    uint32_t bookModified;
};

static String coverCachePath(const String& bookPath) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < bookPath.length(); i++) {
        hash ^= static_cast<uint8_t>(bookPath[i]);
        hash *= 16777619u;
    }
    char name[24];
    snprintf(name, sizeof(name), "/%08lx.bin", static_cast<unsigned long>(hash));
    return String(COVER_CACHE_DIR) + name;
}

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
    else if (lower.endsWith(".cbz")) name.remove(name.length() - 4);
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

struct LibraryRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

static LibraryRect unionLibraryRect(LibraryRect a, LibraryRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

// Where everything on the bookcase screen goes, for drawing, partial
// refreshes and touch alike.
struct Bookcase {
    static constexpr int COLS = 3, ROWS = 3, PER_PAGE = COLS * ROWS;
    static constexpr int HEADER_H = 92, FOOTER_H = 84;
    static constexpr int SIDE = 22;                    // inside the case's posts
    static constexpr int PLANK_H = 12;
    int w, h;
    Bookcase(int width, int height) : w(width), h(height) {}
    int top() const { return HEADER_H + 6; }
    int rowH() const { return (h - FOOTER_H - top()) / ROWS; }
    int cellW() const { return (w - 2 * SIDE) / COLS; }
    // The plank a row's books stand on, at the bottom of the row; titles
    // go above the books, so each reads as the book under it.
    int plankY(int row) const { return top() + (row + 1) * rowH() - PLANK_H; }
    // One book's whole place: its caption, cover space and plank.
    LibraryRect cell(int slot) const {
        return {SIDE + (slot % COLS) * cellW(), top() + (slot / COLS) * rowH(), cellW(), rowH()};
    }
    // The cover box, centered in its cell and standing on the plank.
    LibraryRect cover(int slot) const {
        const LibraryRect c = cell(slot);
        return {c.x + (c.w - LIBRARY_COVER_WIDTH) / 2, plankY(slot / COLS) - LIBRARY_COVER_HEIGHT,
                LIBRARY_COVER_WIDTH, LIBRARY_COVER_HEIGHT};
    }
    int slotAt(int x, int y) const {
        for (int slot = 0; slot < PER_PAGE; ++slot) if (cell(slot).contains(x, y)) return slot;
        return -1;
    }
    // The header's one button ("< Menu", "Settings" or "Update").
    LibraryRect button(bool battery) const { return {w - 20 - 128 - (battery ? 64 : 0), 22, 128, 42}; }
    LibraryRect footer() const { return {0, h - FOOTER_H, w, FOOTER_H}; }
};

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
    _useOpenSans = strcmp(READER_FONT_FAMILY_DEFAULT, "openSans") == 0;
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
            const char* fontFamily = doc["fontFamily"] | READER_FONT_FAMILY_DEFAULT;
            _useOpenSans = strcmp(fontFamily, "openSans") == 0;
            _showChapter = doc["showChapter"] | true;
            _showPageNumber = doc["showPageNumber"] | true;
            _showReadingPercentage = doc["showReadingPercentage"] | true;
            _rightToLeft = doc["mangaRightToLeft"] | false;
            _grayPages = doc["grayPages"] | true;
            _grayZoom = doc["grayZoom"] | false;
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

void AppReader::powerDownRadio() {
    if (WiFi.getMode() == WIFI_OFF) return;
    WebMgr::getInstance().stop();
    delay(50);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    Serial.println("AppReader: WiFi powered down");
}

// Runs HomeServices while the library is the home screen (and stops them
// when a book opens or the app closes).
void AppReader::syncHome() {
    const bool home = _home && _state == VIEW_LIBRARY;
    if (home == _homeShown) return;
    if (!home) { leaveHome(); return; }
    _homeShown = true;
    HomeServices::getInstance().setLibraryHome(true);
    HomeServices::getInstance().enter();
}

void AppReader::leaveHome() {
    if (!_homeShown) return;
    _homeShown = false;
    HomeServices::getInstance().setLibraryHome(false);
    HomeServices::getInstance().leave();
}

void AppReader::start() {
    _home = !AppSelection::getInstance().launcher();
    _homeShown = false;
    // Inside the app menu the reader keeps the radio off throughout. At home
    // the library keeps Wi-Fi for the web page until a book opens.
    if (!_home) powerDownRadio();
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
    leaveHome();
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
        bool pageBook = false;
#if BOOK32_HAS_PAGE_BOOKS
        pageBook = PageBook::isPageBookName(fileName);
#endif
        if(fileNameLower.endsWith(".epub") || pageBook) {
            BookEntry entry;
            entry.path = "/" + fileName;
            entry.size = static_cast<uint32_t>(file.size());
            entry.modified = static_cast<uint32_t>(file.getLastWrite());
            for (const BookEntry& previous : previousBooks) {
                if (previous.path == entry.path && previous.size == entry.size &&
                    previous.modified == entry.modified) {
                    entry.coverAttempted = previous.coverAttempted;
                    entry.cover = previous.cover;
                    break;
                }
            }
            auto meta = metadata.find(fileName);
            String displayTitle = meta != metadata.end() ? meta->second : "";
            if (displayTitle.length() == 0 && !pageBook) {
                showLibraryLoading("Finding books...", 15);
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
    if (lastItem() < 0) {
        // With nothing to open, make the center button's only useful action Back.
        _selectedBookIndex = _home ? 0 : -1;
    } else if (_selectedBookIndex < 0 || bookOfItem(_selectedBookIndex) == -2) {
        _selectedBookIndex = 0;
    }
    _previousBookIndex = _selectedBookIndex;
    if (metadataChanged) saveBookMetadata(metadata);
    pruneCoverCache();
}

void AppReader::showLibraryLoading(const char* status, uint8_t progress) {
    drawLoadingScreen("Opening eReader", status, progress, !_libraryLoadingShown);
    _libraryLoadingShown = true;
}

bool AppReader::loadCachedCover(BookEntry& book, int width, int height) {
    File file = EbookFS.open(coverCachePath(book.path), FILE_READ);
    if (!file) return false;
    CoverCacheHeader header = {};
    bool valid = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                 header.magic == COVER_CACHE_MAGIC && header.version == COVER_CACHE_VERSION &&
                 header.maxWidth == width && header.maxHeight == height &&
                 header.bookSize == book.size && header.bookModified == book.modified &&
                 header.width <= width && header.height <= height;
    if (valid && header.width > 0 && header.height > 0) {
        std::shared_ptr<EpubBitmap> cover(new (std::nothrow) EpubBitmap());
        size_t bytes = static_cast<size_t>((header.width + 7) / 8) * header.height;
        valid = cover && cover->allocate(header.width, header.height) &&
                file.read(cover->pixels(), bytes) == bytes;
        if (valid) book.cover = cover;
    }
    file.close();
    if (valid) book.coverAttempted = true;
    return valid;
}

void AppReader::saveCachedCover(const BookEntry& book, int width, int height) {
    if (!EbookFS.exists(COVER_CACHE_DIR)) EbookFS.mkdir(COVER_CACHE_DIR);
    File file = EbookFS.open(coverCachePath(book.path), FILE_WRITE);
    if (!file) return;
    const bool hasCover = book.cover && book.cover->valid();
    CoverCacheHeader header = {
        COVER_CACHE_MAGIC, COVER_CACHE_VERSION,
        static_cast<uint16_t>(width), static_cast<uint16_t>(height),
        static_cast<uint16_t>(hasCover ? book.cover->width : 0),
        static_cast<uint16_t>(hasCover ? book.cover->height : 0),
        book.size, book.modified
    };
    file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    if (hasCover) {
        file.write(const_cast<EpubBitmap&>(*book.cover).pixels(),
                   static_cast<size_t>(book.cover->stride()) * book.cover->height);
    }
    file.close();
}

void AppReader::pruneCoverCache() {
    File dir = EbookFS.open(COVER_CACHE_DIR);
    if (!dir || !dir.isDirectory()) return;
    std::vector<String> stale;
    File file = dir.openNextFile();
    while (file) {
        String path = String(COVER_CACHE_DIR) + "/" + normalizedBookName(file.name());
        bool used = false;
        for (const BookEntry& book : _books) {
            if (coverCachePath(book.path) == path) {
                used = true;
                break;
            }
        }
        if (!used) stale.push_back(path);
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    for (const String& path : stale) EbookFS.remove(path);
}

void AppReader::drawPlainCover(Book32Display& display, int x, int y, int w, int h, const String& title) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    display.fillRect(x, y, 9, h, GxEPD_BLACK);         // the spine
    display.drawFastVLine(x + 12, y, h, GxEPD_BLACK);
    // The title on a framed label, a third of the way down.
    const int labelX = x + 18, labelW = w - 24;
    std::vector<String> lines = wrapLibraryTitle(display, title, &FreeSans9pt7b, labelW - 10, 4);
    const int lineH = FreeSans9pt7b.yAdvance - 3;
    const int labelH = int(lines.size()) * lineH + 14;
    const int labelY = y + max(10, (h - labelH) / 3);
    display.drawRect(labelX, labelY, labelW, labelH, GxEPD_BLACK);
    int baseline = labelY + 7 + lineH - 4;
    for (const String& line : lines) {
        const int lineW = textWidthForFont(display, line.c_str(), &FreeSans9pt7b);
        drawTextWithFont(display, line.c_str(), labelX + (labelW - lineW) / 2, baseline, &FreeSans9pt7b, GxEPD_BLACK);
        baseline += lineH;
    }
    display.drawFastHLine(labelX + 6, y + h - 14, labelW - 12, GxEPD_BLACK);
}

// Settings (a gear) or Update (an arrow into a tray), drawn as a book.
void AppReader::drawActionBook(Book32Display& display, int x, int y, int w, int h, LibraryButton action) {
    display.fillRect(x + w, y + 4, 3, h - 4, GxEPD_BLACK);  // shadow
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    display.fillRect(x, y, 9, h, GxEPD_BLACK);
    display.drawFastVLine(x + 12, y, h, GxEPD_BLACK);
    const int cx = x + 12 + (w - 12) / 2, cy = y + h * 2 / 5;
    if (action == LibraryButton::SETTINGS) {
        for (int tooth = 0; tooth < 8; ++tooth) {
            const float a = tooth * PI / 4;
            display.fillCircle(cx + int(25 * cosf(a)), cy + int(25 * sinf(a)), 7, GxEPD_BLACK);
        }
        display.fillCircle(cx, cy, 24, GxEPD_BLACK);
        display.fillCircle(cx, cy, 10, GxEPD_WHITE);
    } else {
        display.fillRect(cx - 6, cy - 28, 12, 26, GxEPD_BLACK);
        display.fillTriangle(cx - 18, cy - 4, cx + 18, cy - 4, cx, cy + 16, GxEPD_BLACK);
        display.fillRect(cx - 26, cy + 22, 52, 5, GxEPD_BLACK);
        display.fillRect(cx - 26, cy + 10, 5, 14, GxEPD_BLACK);
        display.fillRect(cx + 21, cy + 10, 5, 14, GxEPD_BLACK);
    }
    display.drawFastHLine(x + 20, y + h - 14, w - 28, GxEPD_BLACK);
}

void AppReader::loadBookCover(BookEntry& book, int width, int height) {
    if (book.coverAttempted) return;
    book.coverAttempted = true;

#if BOOK32_HAS_PAGE_BOOKS
    if (PageBook::isPageBookName(book.path)) {  // The first page is the cover
        PageBook pages;
        size_t size = 0;
        uint8_t* data = pages.open("/ebooks" + book.path) ? pages.readPage(0, size) : nullptr;
        if (data) {
            std::shared_ptr<EpubBitmap> cover(new (std::nothrow) EpubBitmap());
            if (cover && EpubImageDecoder::decode(data, size, width, height, *cover)) book.cover = cover;
            free(data);
        }
        saveCachedCover(book, width, height);
        return;
    }
#endif
    EpubLoader loader;
    String fullPath = "/ebooks" + book.path;
    if (!loader.open(fullPath.c_str(), false)) return;
    String coverHref = loader.getCoverHref();
    if (coverHref.length() > 0) {
        std::shared_ptr<EpubBitmap> cover(new (std::nothrow) EpubBitmap());
        if (cover && loader.decodeImage(coverHref, width, height, *cover)) book.cover = cover;
    }
    loader.close();
    // Cache "no cover" too, so coverless books aren't reopened every restart.
    saveCachedCover(book, width, height);
}

bool AppReader::drawSleepCover(const String& message) {
    // The book most recently opened, whether or not it is still open.
    String lastBook;
    File progress = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (progress) {
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, progress)) lastBook = doc["lastBook"] | "";
        progress.close();
    }
    if (lastBook.length() == 0 || !EbookFS.exists(lastBook)) return false;

    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    const int screenW = display.width();
    const int screenH = display.height();
    // Leave a strip under the cover for the wake message.
    const int messageStrip = message.length() ? 56 : 0;
    const int margin = 12;
    const int maxW = screenW - 2 * margin;
    const int maxH = screenH - 2 * margin - messageStrip;

    EpubBitmap cover;
    bool decoded = false;
#if BOOK32_HAS_PAGE_BOOKS
    if (PageBook::isPageBookName(lastBook)) {
        PageBook pages;
        size_t size = 0;
        uint8_t* data = pages.open("/ebooks" + lastBook) ? pages.readPage(0, size) : nullptr;
        if (data) {
            decoded = EpubImageDecoder::decode(data, size, maxW, maxH, cover, EpubImageQuality::Cover);
            free(data);
        }
    } else
#endif
    {
        EpubLoader loader;
        String fullPath = "/ebooks" + lastBook;
        if (!loader.open(fullPath.c_str(), false)) return false;
        String coverHref = loader.getCoverHref();
        decoded = coverHref.length() &&
                  loader.decodeImage(coverHref, maxW, maxH, cover, EpubImageQuality::Cover);
        loader.close();
    }
    if (!decoded || !cover.valid()) return false;

    const int coverX = (screenW - cover.width) / 2;
    const int coverY = margin + (maxH - cover.height) / 2;
    // Every board shows the cover in true gray, each display mapping it to
    // its own tones. The Sticky and Book32 pulse it in over the white area
    // after the message is up; the LILYGO composes one 16-level frame.
    auto drawMessage = [&]() {
        if (!messageStrip) return;
        display.setFont(&FreeSans12pt7b);
        display.setTextColor(GxEPD_BLACK);
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(message.c_str(), 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((screenW - bw) / 2 - bx, screenH - margin - messageStrip / 2 + bh / 2 - (bh + by));
        display.print(message);
    };
    bool grayShown = false;
    if (cover.gray()) {
#if defined(BOARD_LILYGO_T5S3_PRO)
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        drawMessage();
#else
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            drawMessage();
        } while (display.nextPage());
#endif
        grayShown = display.drawGrayCover(cover.gray(), coverX, coverY, cover.width, cover.height);
    }
    if (!grayShown) {
        // The dithered cover.
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            cover.draw(display, coverX, coverY);
            drawMessage();
        } while (display.nextPage());
    }
    Serial.printf("Sleep cover: %s (%dx%d%s)\n", lastBook.c_str(), cover.width, cover.height,
                  grayShown ? ", grayscale" : "");
    return true;
}

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
#if defined(BOARD_LILYGO_T5S3_PRO)
    if (action == INPUT_BACK) {
        if (_state == VIEW_READING) backToLibrary();
        else if (!_home) { markProgressInactive(); AppMgr::getInstance().switchTo(0); }
        return;
    }
#endif
    if (_state == VIEW_LIBRARY) {
        // Index -1 is the header button (when there is one), 0+ the books.
        const int first = _home ? 0 : -1;
        const int maxIndex = lastItem();
        if (action == INPUT_NEXT || action == INPUT_PREV) {
            if (maxIndex < first) return;
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex += action == INPUT_NEXT ? 1 : -1;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = first;
            if (_selectedBookIndex < first) _selectedBookIndex = maxIndex;
            _librarySelectionOnlyRedraw = _booksScanned;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            const int book = bookOfItem(_selectedBookIndex);
            if (_selectedBookIndex == -1) {
                pressLibraryButton(LibraryButton::MENU);
            } else if (book == -1) {
                pressLibraryButton(libraryButton());
            } else if (book >= 0 && book < static_cast<int>(_books.size())) {
                openBook(_books[book].path.c_str());
            }
        }
    } else if (_state == VIEW_READING) {
        _pageTurnStartedAt = millis();
#if BOOK32_HAS_PAGE_BOOKS
        if (_pageBook && (action == INPUT_NEXT || action == INPUT_PREV)) {
            stepPageBook(action == INPUT_NEXT);
            return;
        }
#endif
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) backToLibrary();
    }
}

void AppReader::backToLibrary() {
    closeBook();
    _state = VIEW_LIBRARY;
    _libraryFirstDraw = true;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
}

AppReader::LibraryButton AppReader::libraryButton() const {
#if BOOK32_HAS_TOUCH
    return LibraryButton::SETTINGS;
#else
    return HomeServices::getInstance().updateAvailable() ? LibraryButton::UPDATE : LibraryButton::NONE;
#endif
}

void AppReader::pressLibraryButton(LibraryButton action) {
    SoundMgr::getInstance().beep();
    switch (action) {
    case LibraryButton::MENU:
        markProgressInactive();
        AppMgr::getInstance().switchTo(0);
        break;
    case LibraryButton::SETTINGS:
        AppMgr::getInstance().switchTo("Settings");
        break;
    case LibraryButton::UPDATE:
        GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
        break;
    case LibraryButton::NONE:
        break;
    }
}

void AppReader::handleTouch(uint16_t x, uint16_t y) {
    if (_state == VIEW_READING) {
        _pageTurnStartedAt = millis();
#if BOOK32_HAS_PAGE_BOOKS
        if (_pageBook) {
            pageBookTouch(x, y);
            return;
        }
#endif
        // Invisible page-turn zones cover the outer thirds of the page.
        if (x < SCREEN_WIDTH / 3) prevPage();
        else if (x >= (SCREEN_WIDTH * 2) / 3) nextPage();
        return;
    }

    const Bookcase shelf(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!_home && shelf.button(false).contains(x, y)) {
        pressLibraryButton(LibraryButton::MENU);
        return;
    }
    if (shelf.footer().contains(x, y)) {  // Prev and Next
        const int page = _scrollOffset / max(1, _libraryPageSize);
        if (x < SCREEN_WIDTH / 3) showLibraryPage(page - 1);
        else if (x >= (SCREEN_WIDTH * 2) / 3) showLibraryPage(page + 1);
        return;
    }
    const int slot = shelf.slotAt(x, y);
    const int item = slot < 0 ? -1 : _scrollOffset + slot;
    const int book = bookOfItem(item);
    if (book == -2) return;
    _selectedBookIndex = item;
    if (book < 0) { pressLibraryButton(libraryButton()); return; }
    SoundMgr::getInstance().beep();
    openBook(_books[book].path);
}

int AppReader::libraryPages() const {
    const int per = booksPerPage();
    return max(1, (static_cast<int>(_books.size()) + per - 1) / per);
}

int AppReader::bookOfItem(int item) const {
    if (item < 0) return -2;
    const int page = item / LIBRARY_PER_PAGE, slot = item % LIBRARY_PER_PAGE;
    if (page >= libraryPages()) return -2;
    const int first = page * booksPerPage();
    const int onPage = min(booksPerPage(), static_cast<int>(_books.size()) - first);
    if (slot < onPage) return first + slot;
    return shelfAction() && slot == onPage ? -1 : -2;
}

int AppReader::lastItem() const {
    const int page = libraryPages() - 1;
    const int onPage = max(0, min(booksPerPage(), static_cast<int>(_books.size()) - page * booksPerPage()));
    const int count = onPage + (shelfAction() ? 1 : 0);
    return count ? page * LIBRARY_PER_PAGE + count - 1 : -1;
}

void AppReader::showLibraryPage(int page) {
    const int pageSize = LIBRARY_PER_PAGE;
    if (page < 0 || page >= libraryPages() || lastItem() < 0) return;
    SoundMgr::getInstance().beep();
    _previousBookIndex = _selectedBookIndex;
    _selectedBookIndex = page * pageSize;  // The selection moves to the page's first book
    _scrollOffset = page * pageSize;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
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
    // Reading keeps the radio off (at home the library had it on).
    leaveHome();
    powerDownRadio();
    String fullPath = "/ebooks" + path;
    drawLoadingScreen("Opening book", "Indexing chapters...", 15, _libraryFirstDraw);
    closeBook(false);
    _currentBookPath = path;
    _currentBookFingerprint = fingerprintBook(path, _currentBookSize);
#if BOOK32_HAS_PAGE_BOOKS
    if (PageBook::isPageBookName(path)) return openPageBook(path, restoreProgress);
#endif
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

int AppReader::loadPageProgress(const String& path) {
    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return 0;
    DynamicJsonDocument doc(4096);
    const bool parsed = !deserializeJson(doc, file);
    file.close();
    if (!parsed) return 0;
    JsonObject saved = doc["books"][path];
    if (saved.isNull() || (saved.containsKey("bookFingerprint") &&
                           saved["bookFingerprint"].as<uint32_t>() != _currentBookFingerprint)) return 0;
    return saved["pageIndex"] | 0;
}

void AppReader::queueProgressSave() {
    _progressSavePending = true;
    _lastPageTurnAt = millis();
}

void AppReader::saveReadingProgress(bool resumeOnBoot) {
    _progressSavePending = false;
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
#if BOOK32_HAS_PAGE_BOOKS
    if (_pageBook) {
        saved["pageIndex"] = _pageIndex;
        saved["pageCount"] = _pageBook->pageCount();
        saved["chapter"] = 0;
        saved["bookFingerprint"] = _currentBookFingerprint;
        saved["globalPage"] = _pageIndex + 1;
        saved["updatedAt"] = millis();
    } else {
#endif
    saved["chapter"] = _currentChapter;
    saved["nodeIndex"] = _currentPagePointer.nodeIndex;
    saved["charOffset"] = _currentPagePointer.charOffset;
    saved["visibleOffset"] = currentVisibleOffset();
    saved["positionVersion"] = 3;
    saved["bookFingerprint"] = _currentBookFingerprint;
    saved["globalPage"] = _globalPageNumber;
    saved["updatedAt"] = millis();
#if BOOK32_HAS_PAGE_BOOKS
    }
#endif

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
    } else if (_progressSavePending && _state == VIEW_READING) {
        saveReadingProgress(true);
    }
    _progressSavePending = false;
    if (_epubLoader) { _epubLoader->close(); delete _epubLoader; _epubLoader = nullptr; }
    if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
#if BOOK32_HAS_PAGE_BOOKS
    closePageBook();
#endif
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
        queueProgressSave();
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
            queueProgressSave();
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
        queueProgressSave();
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
            queueProgressSave();
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
#if BOOK32_HAS_PAGE_BOOKS
    else if (_pageBook) drawPageBook(false);
#endif
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

// A shelf plank: a solid top edge over a 50% wood-grain band.
static void drawPlank(Book32Display& display, int x, int y, int w) {
    display.fillRect(x, y, w, 3, GxEPD_BLACK);
    for (int row = 3; row < Bookcase::PLANK_H - 2; ++row)
        for (int px = x + (row & 1); px < x + w; px += 2) display.drawPixel(px, y + row, GxEPD_BLACK);
    display.fillRect(x, y + Bookcase::PLANK_H - 2, w, 2, GxEPD_BLACK);
}

// A compact battery: four upright bars, one filled per quarter of charge,
// with the percentage to their right. `baseline` is the text's baseline.
static void drawBattery(Book32Display& display, int x, int baseline) {
    const BatteryStatus bat = BatteryMgr::getInstance().getStatus();
    const int filled = constrain((bat.percentage + 24) / 25, 0, 4);
    for (int bar = 0; bar < 4; ++bar) {
        const int bx = x + bar * 7;
        if (bar < filled) display.fillRect(bx, baseline - 13, 4, 13, GxEPD_BLACK);
        else display.drawRect(bx, baseline - 13, 4, 13, GxEPD_BLACK);
    }
    char text[8];
    snprintf(text, sizeof(text), "%d%%", bat.percentage);
    FontMgr::getInstance().drawText(display, text, x + 33, baseline, FONT_SIZE_SMALL, GxEPD_BLACK);
}

void AppReader::drawLibrary() {
    const bool initialLoad = !_booksScanned;
    const unsigned long libraryStartedAt = millis();
    if (initialLoad) {
        // Listing the folder and reading cached titles/covers is quick, so the
        // loading screen only appears once a new book actually needs work.
        _libraryLoadingShown = false;
        scanBooks();
        _booksScanned = true;
    }
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    const Bookcase shelf(display.width(), display.height());
    const int screenW = display.width(), screenH = display.height();

    // The library shows a bookcase of books at a time: the one holding the
    // selection, or the one chosen with the Prev and Next buttons.
    static_assert(Bookcase::PER_PAGE == LIBRARY_PER_PAGE, "one bookcase per library page");
    const int perPage = Bookcase::PER_PAGE;
    _libraryPageSize = perPage;
    const int pageCount = libraryPages();
    int previousScrollOffset = _scrollOffset;
    if (_selectedBookIndex >= 0) _scrollOffset = (_selectedBookIndex / perPage) * perPage;
    _scrollOffset = constrain(_scrollOffset, 0, (pageCount - 1) * perPage);
    if (_scrollOffset != previousScrollOffset) _librarySelectionOnlyRedraw = false;

    const int lastVisibleItem = _scrollOffset + perPage;  // empty places are skipped
    bool coversNeedLoading = false;
    for (int item = _scrollOffset; item < lastVisibleItem; item++) {
        const int index = bookOfItem(item);
        if (index >= 0 && !_books[index].coverAttempted &&
            !loadCachedCover(_books[index], LIBRARY_COVER_WIDTH, LIBRARY_COVER_HEIGHT)) {
            coversNeedLoading = true;
        }
    }
    if (initialLoad && coversNeedLoading) {
        showLibraryLoading("Loading book covers...", 60);
    }
    for (int item = _scrollOffset; item < lastVisibleItem; item++) {
        const int index = bookOfItem(item);
        if (index >= 0) loadBookCover(_books[index], LIBRARY_COVER_WIDTH, LIBRARY_COVER_HEIGHT);
    }
    // A loading screen already gave the panel a clean full refresh.
    if (initialLoad && _libraryLoadingShown) _libraryFirstDraw = false;
    if (initialLoad) {
        Serial.printf("AppReader: library ready in %lums (%u books, loading screen %s)\n",
                      millis() - libraryStartedAt, static_cast<unsigned>(_books.size()),
                      _libraryLoadingShown ? "shown" : "skipped");
    }

    const LibraryButton button = libraryButton();
    auto selectionRect = [&](int index) {
        if (index < 0) return shelf.button(false);
        const int slot = index - _scrollOffset;
        return slot >= 0 && slot < perPage ? shelf.cell(slot) : LibraryRect{0, 0, 0, 0};
    };

    // Entering the library replaces an unrelated screen, so it needs the full
    // waveform. Partial refresh is reserved for subsequent changes.
    if (_libraryFirstDraw) {
        display.setFullWindow();
        _libraryFirstDraw = false;
    } else if (_librarySelectionOnlyRedraw) {
        LibraryRect dirty = unionLibraryRect(selectionRect(_previousBookIndex), selectionRect(_selectedBookIndex));
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else if (_libraryFooterOnly && !_libraryHeaderOnly) {
        const LibraryRect footer = shelf.footer();
        display.setPartialWindow(footer.x, footer.y, footer.w, footer.h);
    } else if (_libraryHeaderOnly && !_libraryFooterOnly) {
        display.setPartialWindow(0, 0, screenW, Bookcase::HEADER_H);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }
    _librarySelectionOnlyRedraw = false;
    _libraryFooterOnly = false;
    _libraryHeaderOnly = false;
    _drawnButton = button;
    _shownBattery = BatteryMgr::getInstance().getStatus().percentage;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // === Header: title, count, battery (at home) and the one button ===
        fontMgr.drawText(display, "Library", 20, 52, FONT_SIZE_TITLE, GxEPD_BLACK);
        char countText[24];
        snprintf(countText, sizeof(countText), _books.size() == 1 ? "%d book" : "%d books", (int)_books.size());
        fontMgr.drawText(display, countText, 22, 78, FONT_SIZE_SMALL, GxEPD_BLACK);
        if (!_home) {
            const LibraryRect r = shelf.button(false);
            const bool selected = _selectedBookIndex == -1 && !BOOK32_HAS_TOUCH;
            const char* label = "<  Menu";
            if (selected) display.fillRoundRect(r.x, r.y, r.w, r.h, 8, GxEPD_BLACK);
            display.drawRoundRect(r.x, r.y, r.w, r.h, 8, GxEPD_BLACK);
            display.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 7, GxEPD_BLACK);
            const int labelW = textWidthForFont(display, label, &FreeSans12pt7b);
            drawTextWithFont(display, label, r.x + (r.w - labelW) / 2, r.y + 28, &FreeSans12pt7b,
                             selected ? GxEPD_WHITE : GxEPD_BLACK);
        }

        // === The bookcase: posts, a top board and a plank under each shelf ===
        const int caseLeft = Bookcase::SIDE - 12, caseRight = screenW - Bookcase::SIDE + 12;
        const int caseTop = shelf.top() - 6, caseBottom = shelf.plankY(Bookcase::ROWS - 1) + Bookcase::PLANK_H;
        display.fillRect(caseLeft, caseTop, caseRight - caseLeft, 4, GxEPD_BLACK);
        display.fillRect(caseLeft, caseTop, 5, caseBottom - caseTop, GxEPD_BLACK);
        display.fillRect(caseRight - 5, caseTop, 5, caseBottom - caseTop, GxEPD_BLACK);
        for (int row = 0; row < Bookcase::ROWS; ++row) drawPlank(display, caseLeft, shelf.plankY(row), caseRight - caseLeft);

        if (_books.empty()) {
            const int y = shelf.plankY(shelfAction() ? 1 : 0) - 70;
            const char* empty = "Your bookcase is empty";
            drawTextWithFont(display, empty, (screenW - textWidthForFont(display, empty, &FreeSansBold12pt7b)) / 2, y,
                             &FreeSansBold12pt7b, GxEPD_BLACK);
            fontMgr.drawTextCentered(display, "Upload EPUB books from the web page.", y + 34, FONT_SIZE_SMALL, GxEPD_BLACK);
        }

        // === Books standing on the shelves, each title just above its book,
        // clear of the selection frame ===
        constexpr int CAPTION_GAP = 14;  // the last line's baseline above the book
        for (int item = _scrollOffset; item < lastVisibleItem; item++) {
            const int slot = item - _scrollOffset;
            const LibraryRect cell = shelf.cell(slot), box = shelf.cover(slot);
            const int plankY = shelf.plankY(slot / Bookcase::COLS);
            const bool selected = item == _selectedBookIndex && !BOOK32_HAS_TOUCH;
            const int index = bookOfItem(item);
            if (index == -2) continue;
            if (index < 0) {
                // Settings or Update: a book of its own, after the page's books.
                const String caption = button == LibraryButton::SETTINGS ? String("Settings") :
                                       "Update " + HomeServices::getInstance().updateVersion();
                drawActionBook(display, box.x, plankY - box.h, box.w, box.h, button);
                if (selected) {
                    for (int inset = 0; inset < 3; ++inset)
                        display.drawRoundRect(box.x - 8 + inset, plankY - box.h - 8 + inset, box.w + 19 - 2 * inset,
                                              box.h + 8 - inset, 6, GxEPD_BLACK);
                }
                const GFXfont* font = &FreeSansBold9pt7b;
                const int captionW = textWidthForFont(display, caption.c_str(), font);
                drawTextWithFont(display, caption.c_str(), cell.x + (cell.w - captionW) / 2,
                                 plankY - box.h - CAPTION_GAP, font, GxEPD_BLACK);
                continue;
            }
            const BookEntry& book = _books[index];
            const bool picture = book.cover && book.cover->valid();
            // A picture keeps its own shape; books stand on the plank.
            const int bw = picture ? book.cover->width : box.w, bh = picture ? book.cover->height : box.h;
            const int bx = box.x + (box.w - bw) / 2, by = plankY - bh;
            display.fillRect(bx + bw, by + 4, 3, bh - 4, GxEPD_BLACK);  // shadow
            if (picture) {
                display.fillRect(bx, by, bw, bh, GxEPD_WHITE);
                book.cover->draw(display, bx, by);
                display.drawRect(bx, by, bw, bh, GxEPD_BLACK);
            } else {
                drawPlainCover(display, bx, by, bw, bh, book.title);
            }
            if (selected) {
                for (int inset = 0; inset < 3; ++inset)
                    display.drawRoundRect(bx - 8 + inset, by - 8 + inset, bw + 19 - 2 * inset, bh + 8 - inset, 6,
                                          GxEPD_BLACK);
            }

            const GFXfont* captionFont = selected ? &FreeSansBold9pt7b : &FreeSans9pt7b;
            std::vector<String> lines = wrapLibraryTitle(display, book.title, captionFont, cell.w - 12, 2);
            int baseline = by - CAPTION_GAP - (int(lines.size()) - 1) * 19;
            for (const String& line : lines) {
                const int lineW = textWidthForFont(display, line.c_str(), captionFont);
                drawTextWithFont(display, line.c_str(), cell.x + (cell.w - lineW) / 2, baseline, captionFont, GxEPD_BLACK);
                baseline += 19;
            }
        }

        // === Footer: page buttons when there are several, then InkDeck's
        // version and Wi-Fi (at home) and the battery along the bottom ===
        const int footerY = screenH - Bookcase::FOOTER_H;
        display.drawFastHLine(20, footerY + 4, screenW - 40, GxEPD_BLACK);
#if BOOK32_HAS_TOUCH
        if (pageCount > 1) {
            // Prev and Next buttons (handleTouch: the footer's outer thirds).
            const int page = _scrollOffset / perPage;
            const int buttonW = 100, buttonH = 28, buttonY = footerY + 10;
            auto pageButton = [&](int x, const char* label, bool enabled) {
                if (!enabled) return;
                display.drawRoundRect(x, buttonY, buttonW, buttonH, 8, GxEPD_BLACK);
                display.drawRoundRect(x + 1, buttonY + 1, buttonW - 2, buttonH - 2, 7, GxEPD_BLACK);
                const int labelW = textWidthForFont(display, label, &FreeSans9pt7b);
                drawTextWithFont(display, label, x + (buttonW - labelW) / 2, buttonY + 20, &FreeSans9pt7b, GxEPD_BLACK);
            };
            pageButton(20, "<  Prev", page > 0);
            pageButton(screenW - 20 - buttonW, "Next  >", page < pageCount - 1);
            char pageText[24];
            snprintf(pageText, sizeof(pageText), "Page %d of %d", page + 1, pageCount);
            fontMgr.drawTextCentered(display, pageText, buttonY + 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        }
#endif
        // Two lines in each bottom corner, 22 px from the edges: InkDeck's
        // version over the battery on the left, the web page's address under
        // "Dashboard" on the right (at home; Wi-Fi notices stand alone).
        const int bottomY = screenH - 10, upperY = bottomY - 24;
        drawBattery(display, 22, bottomY);
        if (_home) {
            char version[24];
            snprintf(version, sizeof(version), "InkDeck v%s", SYSTEM_VERSION);
            fontMgr.drawText(display, version, 22, upperY, FONT_SIZE_SMALL, GxEPD_BLACK);
            const String status = HomeServices::getInstance().footerText();
            if (!status.startsWith("WiFi "))  // an address, not "WiFi offline" or "WiFi starting"
                fontMgr.drawTextRight(display, "Dashboard", screenW - 22, upperY, FONT_SIZE_SMALL, GxEPD_BLACK);
            fontMgr.drawTextRight(display, status.c_str(), screenW - 22, bottomY, FONT_SIZE_SMALL, GxEPD_BLACK);
        }

    } while (display.nextPage());
}

void AppReader::drawReading() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    const unsigned long drawStartedAt = millis();

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

    if (_pageTurnStartedAt) {
        const unsigned long now = millis();
        Serial.printf("AppReader: page turn %lums (input to draw %lums, render + panel %lums)\n",
                      now - _pageTurnStartedAt, drawStartedAt - _pageTurnStartedAt, now - drawStartedAt);
        _pageTurnStartedAt = 0;
    }
}

void AppReader::update() {
    syncHome();
    if (_homeShown) {
        // Wi-Fi and update notices in the footer; a new update adds the
        // Book32's Update book.
        if (HomeServices::getInstance().poll()) {
            // A new Update book moves every book along: repaint them all.
            if (libraryButton() == _drawnButton) _libraryFooterOnly = true;
            _needsRedraw = true;
        }
        if (millis() - _lastBatteryPoll >= 30000) {
            _lastBatteryPoll = millis();
            const int battery = BatteryMgr::getInstance().refreshNow().percentage;
            if (_shownBattery >= 0 && abs(battery - _shownBattery) >= 5) {
                _libraryFooterOnly = true;  // the battery sits in the footer
                _needsRedraw = true;
            }
        }
    }
#if BOOK32_HAS_PAGE_BOOKS
    if (_state == VIEW_READING && _pageBook) {
        pollPageGestures();
        // Once the page is up and nothing is happening, prepare the next one,
        // and the full-size picture so zooming starts at once.
        if (!_needsRedraw && !_gestureActive && millis() - _pageDrawnAt > 150) {
            prefetchNextPage();
            ensureFullPage();
#if BOOK32_HAS_LIVE_VIEW
            prepareLiveSources();
#endif
        }
    }
#endif
    // Library rendering is static unless input changes selection.
    if (_progressSavePending && millis() - _lastPageTurnAt >= PROGRESS_SAVE_DELAY_MS) {
        const unsigned long startedAt = millis();
        saveReadingProgress(true);
        Serial.printf("AppReader: progress saved in %lums\n", millis() - startedAt);
    }
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
