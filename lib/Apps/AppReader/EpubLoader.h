#ifndef EPUB_LOADER_H
#define EPUB_LOADER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <unzipLIB.h>
#include "EpubImage.h"

// Text formatting enums
enum TextStyle {
    STYLE_NORMAL,
    STYLE_BOLD,
    STYLE_ITALIC,
    STYLE_BOLD_ITALIC,
    STYLE_HEADER1,
    STYLE_HEADER2,
    STYLE_HEADER3,
    STYLE_HEADER4
};

enum TextAlign {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
    ALIGN_JUSTIFY
};

// Rich text node for formatted content
struct RichTextNode {
    String text;
    TextStyle style;
    TextAlign align;
    bool isListItem;
    bool isBlockStart; // Starts a new paragraph/block
    int indent; 
    
    RichTextNode() : style(STYLE_NORMAL), align(ALIGN_LEFT), isListItem(false), isBlockStart(true), indent(0) {}
};

// Table structures
struct TableCell {
    String content;
    int colspan;
    int rowspan;
    bool isHeader;
    
    TableCell() : colspan(1), rowspan(1), isHeader(false) {}
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct Table {
    std::vector<TableRow> rows;
    int columnCount;
    
    Table() : columnCount(0) {}
};

// Font metadata
struct FontInfo {
    String family;
    String path;
    String style;  // normal, italic, bold, bold-italic
    String format; // ttf, otf, woff, woff2
};

struct ImageNode {
    String href;
    String alt;
    int sourceWidth;
    int sourceHeight;
    int requestedWidth;
    int requestedHeight;
    int widthPercent;
    int heightPercent;
    bool fromSvg;

    ImageNode()
        : sourceWidth(0), sourceHeight(0), requestedWidth(0), requestedHeight(0),
          widthPercent(0), heightPercent(0), fromSvg(false) {}
};

// Content node - can be text, an inline EPUB image, or a table.
enum ContentType {
    CONTENT_TEXT,
    CONTENT_IMAGE,
    CONTENT_TABLE
};

struct ContentNode {
    ContentType type;
    RichTextNode textNode;
    ImageNode imageNode;
    Table table;
    
    ContentNode() : type(CONTENT_TEXT) {}
};

class EpubLoader {
public:
    EpubLoader();
    ~EpubLoader();
    bool open(const char* path);
    void close();
    
    // Metadata getters
    String getTitle();
    String getAuthor();
    String getPublisher();
    String getLanguage();
    String getPublicationDate();
    String getISBN();
    
    // Content getters
    int getChapterCount();
    String getChapterContent(int index);  // Legacy plain text
    std::vector<ContentNode> getChapterContentRich(int index);  // Rich formatted content
    String getCoverHref() const { return coverHref; }
    bool getImageDimensions(const String& href, EpubImageInfo& info);
    bool decodeImage(const String& href, int maxWidth, int maxHeight, EpubBitmap& bitmap);
    
    // Font support
    std::vector<FontInfo> getFonts();
    uint8_t* getFontData(String path, size_t* outSize);

private:
    // Metadata
    String bookTitle;
    String bookAuthor;
    String bookPublisher;
    String bookLanguage;
    String bookPubDate;
    String bookISBN;
    
    // Paths
    String epubPath;
    String opfPath;
    String rootDir; // Directory of the OPF file
    String coverHref;
    
    // Fonts
    std::vector<FontInfo> fonts;
    std::vector<String> hiddenCssClasses;

    struct SpineItem {
        String id;
        String href;
    };

    std::vector<SpineItem> spine;
    std::map<String, String> manifest; // id -> href

    // Allocate UNZIP in PSRAM to avoid memory issues with the 41KB internal buffer
    UNZIP* zip;

    // Helper to parse XML for specific attribute
    String extractAttribute(const String& xml, const String& tag, const String& attr);
    // Helper to get text content of tag
    String extractTagContent(const String& xml, const String& tag);
    // Helper to extract metadata from OPF
    String extractMetadata(const String& xml, const String& tag);

    // Helper to read file from zip
    String readFileFromZip(const char* path);
    std::vector<ContentNode> readRichContentFromZip(const char* path);
    uint8_t* readItemBytes(const String& path, size_t& size, size_t maximumBytes = 6 * 1024 * 1024);
    uint8_t* readItemPrefix(const String& path, size_t& size, size_t maximumBytes = 128 * 1024);
    
    // Rich content parsing
    std::vector<ContentNode> parseHtmlToRichContent(const String& html);
    Table parseTable(const String& tableHtml);
    TextStyle getStyleFromTag(String tag);
    TextAlign getAlignFromStyle(String styleAttr);

    bool parseContainer();
    bool parseOpf();
};

#endif
