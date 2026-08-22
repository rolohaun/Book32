#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "../../Book32_Core/InputMgr.h"
#include <vector>
#include <map>

enum ReaderState {
    VIEW_LIBRARY,
    VIEW_READING
};

struct BookEntry {
    String path;  // Full path to file
    String title; // Display title
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
    void resumeSavedBookOnStart();
    void handleInput(InputAction action);
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
    int _scrollOffset; // For list scrolling
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    bool _resumeSavedBookOnStart;
    int _previousBookIndex;
    void scanBooks();
    void drawLibrary();
    void drawBookTile(Book32Display& display, int x, int y, int w, int h, bool selected);
    
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
    bool _readingFirstDraw;   // Forces a full refresh on the next reading draw
    void loadSettings();
    
    // Reading
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    int _currentChapter;
    int _currentPage; // Current page number within the whole book
    int _globalPageNumber; // Runtime tracking of global page (1-indexed)
    bool _needsRedraw;
    
    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
    RenderResult _currentPageRender;
    bool _currentPageRenderValid;
    
    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    bool loadBookProgress(const String& path, int& chapter, PagePointer& pointer, int& globalPage);
    void saveReadingProgress(bool resumeOnBoot);
    void markProgressInactive();
    void closeBook(bool markInactive = true);
    void loadChapter(int chapterIndex);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif
