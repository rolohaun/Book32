#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "ReaderPageCache.h"
#include "PageBook.h"
#include "PageView.h"
#include "../../Book32_Core/InputMgr.h"
#include <vector>
#include <map>
#include <memory>

enum ReaderState {
    VIEW_LIBRARY,
    VIEW_READING
};

struct BookEntry {
    String path;  // Full path to file
    String title; // Display title
    uint32_t size = 0;      // File size and modification time identify the
    uint32_t modified = 0;  // upload, so a replaced book gets a new cover.
    bool coverAttempted = false;
    std::shared_ptr<EpubBitmap> cover;
};

class AppReader : public App {
public:
    AppReader();
    virtual ~AppReader();
    
    // App Interface
    void start() override;
    void stop() override;
    void update() override; // Main loop: Input handling
    void draw() override;   // Display handling
    
    // Icon
    const uint8_t* getIconImage() override; 
    const char* getName() override { return "eReader"; }

    bool hasBootResume();
    // Sleep screen: draws the cover of the most recently read book full screen
    // with `message` under it. Returns false when there is no usable cover.
    static bool drawSleepCover(const String& message);
    void resumeSavedBookOnStart();
    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);
    void forceRedraw() override;

    // Apply a new reading font size (9/12/18pt) live. Safe to call from the
    // main loop; re-paginates the current page from the saved position.
    void applyFontSize(int pt) override;
    void applyFontFamily(bool useOpenSans) override;

private:
    ReaderState _state;
    
    // Library
    std::vector<BookEntry> _books;
    int _selectedBookIndex;
    int _scrollOffset; // First book shown: the library shows a page of books at a time
    int _libraryPageSize = 5;  // Books per library page (set when drawing)
    void showLibraryPage(int page);
    bool _booksScanned;
    bool _libraryFirstDraw;
    bool _librarySelectionOnlyRedraw;
    bool _resumeSavedBookOnStart;
    int _previousBookIndex;
    void scanBooks();
    void drawLibrary();
    void drawLoadingScreen(const char* title, const char* status, uint8_t progress, bool fullRefresh);
    // A book without a cover picture: a cloth binding with its title.
    void drawPlainCover(Book32Display& display, int x, int y, int w, int h, const String& title);
    // The library is the home screen when no optional app is installed: it
    // keeps Wi-Fi up for the web page until a book opens (HomeServices).
    bool _home = false;        // no main menu: the library is home
    bool _homeShown = false;   // HomeServices currently runs for the library
    bool _libraryFooterOnly = false;
    void syncHome();
    void leaveHome();
    bool _libraryHeaderOnly = false;
    unsigned long _lastBatteryPoll = 0;
    int _shownBattery = -1;
    void powerDownRadio();
    void backToLibrary();
    // The library's own action: "< Menu" in the header (index -1) inside the
    // app menu; at home a book on the shelf, first in the list: Settings on
    // touch boards, Update on the Book32 when one is ready.
    enum class LibraryButton : uint8_t { NONE, MENU, SETTINGS, UPDATE };
    LibraryButton libraryButton() const;
    void drawActionBook(Book32Display& display, int x, int y, int w, int h, LibraryButton action);
    bool shelfAction() const { return _home && libraryButton() != LibraryButton::NONE; }
    // Shelf items: the action's book, then the books. -1 for the action.
    int shelfItems() const { return static_cast<int>(_books.size()) + (shelfAction() ? 1 : 0); }
    int bookOfItem(int item) const { return shelfAction() ? item - 1 : item; }
    LibraryButton _drawnButton = LibraryButton::NONE;
    void pressLibraryButton();
    void loadBookCover(BookEntry& book, int width, int height);
    // Decoded covers persist in /.covers so a book is only decoded once per
    // upload instead of after every restart.
    bool loadCachedCover(BookEntry& book, int width, int height);
    void saveCachedCover(const BookEntry& book, int width, int height);
    void pruneCoverCache();
    // The library's loading screen is only shown when there is slow work to do.
    bool _libraryLoadingShown = false;
    void showLibraryLoading(const char* status, uint8_t progress);

    // Global Pagination
    int _totalBookPages;
    std::vector<int> _chapterPageCounts;
    void calculateTotalPages();
    int getGlobalPageNumber();
    
    // Settings
    int _refreshEveryNPages;
    int _pageTurnsSinceRefresh;
    int _fontSizePt;          // Reading body font size in points (9/12/18)
    bool _useOpenSans;        // False uses the original FreeSans reader font
    bool _showChapter;
    bool _showPageNumber;
    bool _showReadingPercentage;
    bool _readingFirstDraw;   // Forces a full refresh on the next reading draw
    void loadSettings();
    
    // Reading
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    uint32_t _currentBookSize;
    uint32_t _currentBookFingerprint;
    int _currentChapter;
    int _currentPage; // Current page number within the whole book
    int _globalPageNumber; // Runtime tracking of global page (1-indexed)
    bool _needsRedraw;
    
    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
    ReaderPageCache _pageCache;
    RenderResult _currentPageRender;
    bool _currentPageRenderValid;
    
    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    bool loadBookProgress(const String& path, int& chapter, PagePointer& pointer, int& globalPage,
                          uint32_t& visibleOffset, bool& hasVisibleOffset);
    void saveReadingProgress(bool resumeOnBoot);
    // Page turns defer the progress write until the page is on screen and
    // turning has paused; closeBook() flushes it on every exit path.
    void queueProgressSave();
    bool _progressSavePending = false;
    unsigned long _lastPageTurnAt = 0;
    unsigned long _pageTurnStartedAt = 0;  // For the per-turn timing log
    void markProgressInactive();
    void closeBook(bool markInactive = true);
    void loadChapter(int chapterIndex);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
    uint32_t currentVisibleOffset() const;
    uint32_t readerLayoutKey() const;
    void rememberCurrentPage();
    void rebuildPageHistory();
    void resetPageCacheForLayout();

    // Page books (AppReaderPages.cpp): CBZ manga and converted PDFs, one
    // page image at a time with pinch to zoom.
    PageBook* _pageBook = nullptr;
    int _pageIndex = 0;
    // The current page: its compressed image, the screen-size picture shown
    // when not zoomed (quick: JPEGs decode straight to half size), and the
    // picture at up to twice the screen size, decoded on the first zoom.
    uint8_t* _pageData = nullptr;
    size_t _pageDataSize = 0;
    uint8_t* _pageFit = nullptr;
    int _pageFitW = 0, _pageFitH = 0;
    uint8_t* _pageGray = nullptr;
    int _pageGrayW = 0, _pageGrayH = 0;
    PageView _pageView;
    bool _rightToLeft = false;     // Manga order: next is on the left
    bool _grayPages = true;        // LILYGO: whole pages in 16 grays
    bool _grayZoom = false;        // LILYGO: zoomed views in 16 grays too (full refresh each)
    int loadPageProgress(const String& path);
#if BOOK32_HAS_PAGE_BOOKS
    bool openPageBook(const String& path, bool restoreProgress);
    void closePageBook();
    bool loadPageImage(bool keepZoom, bool forward);
    bool ensureFullPage();
    void freePage();
    // The next page in reading order, read and decoded while the current one
    // is on screen, so turning only waits for the display.
    struct PreparedPage {
        int index = -1;
        uint8_t* data = nullptr;
        size_t size = 0;
        uint8_t* fit = nullptr;
        int fitW = 0, fitH = 0, width = 0, height = 0;
    };
    PreparedPage _nextPage;
    bool _lastTurnForward = true;
    unsigned long _pageDrawnAt = 0;
    void preparePage(int index, PreparedPage& page);
    static void freePrepared(PreparedPage& page);
    void prefetchNextPage();
    bool turnPageBook(bool forward, bool keepZoom);
    void stepPageBook(bool forward);
    void pageBookTouch(uint16_t x, uint16_t y);
    void pollPageGestures();
    void drawPageBook(bool preview);
    // Gesture state
    bool _gestureActive = false, _gestureMoved = false, _gestureConsumed = false;
    bool _gestureViewChanged = false, _viewChangedSincePreview = false, _gesturePreviewed = false;
    bool _pinchActive = false;
    uint8_t _gestureFingers = 0;
    int _dragStartX = 0, _dragStartY = 0, _dragLastX = 0, _dragLastY = 0;
    float _pinchStartDistance = 1, _pinchStartZoom = 1, _pinchPageX = 0, _pinchPageY = 0;
    unsigned long _gestureEndedAt = 0, _gestureZeroSince = 0, _lastPreviewAt = 0;
    bool _gesturePinched = false;  // After a pinch, the last finger up doesn't drag
#if BOOK32_HAS_LIVE_VIEW
    // Live frames while fingers move (StickyDisplay::liveFrame)
    // Frames are drawn on the other core while this one sends the last one.
    void renderLiveView(uint8_t* out, const PageView& view);
    void prepareLiveSources();
    void startLiveRender();
    void finishLiveView();
    static void liveWorker(void* self);
    TaskHandle_t _liveTask = nullptr;
    SemaphoreHandle_t _liveRequest = nullptr;
    uint8_t* _liveBuffer[2] = {nullptr, nullptr};
    PageView _liveJobView;
    int _liveJobBuffer = 0;
    int _liveShown = 1;                   // The buffer last sent to the panel
    volatile bool _liveRendering = false;
    volatile int _liveReady = -1;         // A drawn buffer waiting to be sent
    // The page pictures turned sideways (column after column): the panel's
    // rows are the portrait page's columns, so live frames read them in order.
    uint8_t* _pageFitT = nullptr;
    uint8_t* _pageGrayT = nullptr;
    int16_t* _liveCols = nullptr;
    int16_t* _liveRows = nullptr;
    unsigned _liveFrames = 0;
    unsigned long _liveRenderUs = 0;
    unsigned long _liveLastFrameAt = 0;
    unsigned long _liveStartedAt = 0;
#endif
#endif
};

#endif
