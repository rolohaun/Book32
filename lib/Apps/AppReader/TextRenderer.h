#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>
#include <vector>
#include <memory>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "DisplayMgr.h"
#include "EpubLoader.h"
#include "ReaderPosition.h"

struct RenderResult {
    int nodesConsumed;
    int charsConsumedInLastNode;
    bool pageFull;
    int nextNodeIndex;
    int nextCharOffset;
};

struct RenderedLine {
    int x, y, fontSize;
    bool isBold;
    String text;
};

struct RenderedImage {
    String href;
    String alt;
    int x, y, width, height;
};

struct DecodedImageCacheEntry {
    String href;
    int maxWidth, maxHeight;
    std::shared_ptr<EpubBitmap> bitmap;
};

class TextRenderer {
public:
    TextRenderer(int width, int height, int fontSize = 9, bool useOpenSans = false);
    
    bool loadFont(const uint8_t* data, size_t size);
    // Body text size in points. Supported: 9 (small), 12 (medium), 18 (large).
    // Invalidates caches so word-wrap and pagination recompute at the new size.
    void setFontSize(int size);
    int getFontSize() const { return _fontSize; }
    void setFontFamily(bool useOpenSans);
    void setImageSource(EpubLoader* source) { _imageSource = source; }
    bool isUsingOpenSans() const { return _useOpenSans; }

    void calculateDimensions();

    // New Dynamic Rendering
    RenderResult renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                     int startNode, int startOffset, int pageNum, int pageNumForDisplay, bool draw = true);

    void clearCache();

    // Keep legacy for now to avoid breaking AppReader during transition
    std::vector<String> paginate(const String& text);
    void renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages);
    
    std::vector<String> paginateRich(std::vector<ContentNode>& content);
    void renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages);

private:
    int _width;
    int _height;
    int _fontSize;
    bool _useOpenSans;
    int _charsPerLine;
    int _linesPerPage;
    int _lineHeight; 
    
    bool _fontLoaded = true; // GFX fonts are always "loaded"
    std::vector<RenderedLine> _lineCache;
    std::vector<RenderedImage> _renderedImages;
    std::vector<DecodedImageCacheEntry> _decodedImages;
    int _cachedPage = -1;
    RenderResult _cachedResult = {0, 0, false, 0, 0};
    bool _hasCachedResult = false;
    
    // Fast character width cache
    uint8_t _gfxCharWidths[128];
    const GFXfont* _lastGFXFont = nullptr;

    const GFXfont* getGFXFont(TextStyle style, int& lineHeight);
    void drawImage(Book32Display& display, const RenderedImage& image);
    void imageDimensions(const ImageNode& image, int maxWidth, int maxHeight, int& width, int& height) const;

    std::vector<String> wrapText(const String& text);
    void renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY);
    void renderTable(Book32Display& display, Table& table, int& y, int maxY);
    EpubLoader* _imageSource = nullptr;
};

#endif
