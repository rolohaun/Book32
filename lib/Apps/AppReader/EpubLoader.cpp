#include "EpubLoader.h"
#include "Book32FS.h"
#include <LittleFS.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <new>

#ifndef ZIP_SUCCESS
#define ZIP_SUCCESS 0
#endif

static int zipFd = -1;

namespace {

String decodeUriEscapes(String value) {
    value.replace("&amp;", "&");
    String decoded;
    decoded.reserve(value.length());
    for (int i = 0; i < static_cast<int>(value.length()); i++) {
        if (value[i] == '%' && i + 2 < static_cast<int>(value.length()) &&
            isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            decoded += static_cast<char>(strtoul(hex, nullptr, 16));
            i += 2;
        } else {
            decoded += value[i];
        }
    }
    return decoded;
}

String normalizeZipPath(String path) {
    path.replace('\\', '/');
    while (path.startsWith("/")) path.remove(0, 1);

    std::vector<String> segments;
    int start = 0;
    while (start <= static_cast<int>(path.length())) {
        int slash = path.indexOf('/', start);
        if (slash < 0) slash = path.length();
        String segment = path.substring(start, slash);
        if (segment.length() == 0 || segment == ".") {
            // Nothing to add.
        } else if (segment == "..") {
            if (!segments.empty()) segments.pop_back();
        } else {
            segments.push_back(segment);
        }
        if (slash >= static_cast<int>(path.length())) break;
        start = slash + 1;
    }

    String normalized;
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) normalized += '/';
        normalized += segments[i];
    }
    return normalized;
}

String resolveZipHref(const String& documentPath, String href) {
    href = decodeUriEscapes(href);
    int fragment = href.indexOf('#');
    if (fragment >= 0) href = href.substring(0, fragment);
    int query = href.indexOf('?');
    if (query >= 0) href = href.substring(0, query);
    href.trim();
    String lower = href;
    lower.toLowerCase();
    if (href.length() == 0 || lower.startsWith("data:") || lower.startsWith("http:") ||
        lower.startsWith("https:")) return "";
    if (href.startsWith("/")) return normalizeZipPath(href);

    int slash = documentPath.lastIndexOf('/');
    String base = slash >= 0 ? documentPath.substring(0, slash + 1) : "";
    return normalizeZipPath(base + href);
}

void appendUtf8(String& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

String decodeHtmlEntities(const String& input) {
    String output;
    output.reserve(input.length());
    for (int i = 0; i < static_cast<int>(input.length()); i++) {
        if (input[i] != '&') {
            output += input[i];
            continue;
        }

        int end = input.indexOf(';', i + 1);
        if (end < 0 || end - i > 12) {
            output += input[i];
            continue;
        }

        String entity = input.substring(i + 1, end);
        if (entity == "amp") output += '&';
        else if (entity == "lt") output += '<';
        else if (entity == "gt") output += '>';
        else if (entity == "quot") output += '"';
        else if (entity == "apos") output += '\'';
        else if (entity == "nbsp" || entity == "ensp" || entity == "emsp") output += ' ';
        else if (entity == "mdash") output += " -- ";
        else if (entity == "ndash") output += " - ";
        else if (entity == "hellip") output += "...";
        else if (entity == "lsquo" || entity == "rsquo") output += '\'';
        else if (entity == "ldquo" || entity == "rdquo") output += '"';
        else if (entity.startsWith("#")) {
            bool hex = entity.length() > 2 && (entity[1] == 'x' || entity[1] == 'X');
            const char* digits = entity.c_str() + (hex ? 2 : 1);
            char* parseEnd = nullptr;
            uint32_t value = strtoul(digits, &parseEnd, hex ? 16 : 10);
            if (parseEnd && *parseEnd == '\0' && value > 0 && value <= 0x10FFFF) appendUtf8(output, value);
            else output += input.substring(i, end + 1);
        } else {
            output += input.substring(i, end + 1);
        }
        i = end;
    }
    return output;
}

String stripMarkup(String value) {
    String clean;
    clean.reserve(value.length());
    bool inTag = false;
    for (int i = 0; i < static_cast<int>(value.length()); i++) {
        const char c = value[i];
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            clean += c;
        }
    }
    clean = decodeHtmlEntities(clean);
    clean.replace("\r", " ");
    clean.replace("\n", " ");
    clean.replace("\t", " ");
    while (clean.indexOf("  ") >= 0) clean.replace("  ", " ");
    clean.trim();
    return clean;
}

String attributeValue(const String& tag, const char* attribute) {
    String lower = tag;
    lower.toLowerCase();
    String key = String(attribute);
    key.toLowerCase();
    int pos = lower.indexOf(key);
    while (pos >= 0) {
        int cursor = pos + key.length();
        while (cursor < static_cast<int>(tag.length()) && isspace(static_cast<unsigned char>(tag[cursor]))) cursor++;
        if (cursor < static_cast<int>(tag.length()) && tag[cursor] == '=') {
            cursor++;
            while (cursor < static_cast<int>(tag.length()) && isspace(static_cast<unsigned char>(tag[cursor]))) cursor++;
            if (cursor >= static_cast<int>(tag.length())) return "";
            char quote = tag[cursor];
            if (quote == '"' || quote == '\'') {
                int end = tag.indexOf(quote, cursor + 1);
                return end >= 0 ? tag.substring(cursor + 1, end) : "";
            }
            int end = cursor;
            while (end < static_cast<int>(tag.length()) && !isspace(static_cast<unsigned char>(tag[end])) && tag[end] != '>') end++;
            return tag.substring(cursor, end);
        }
        pos = lower.indexOf(key, pos + key.length());
    }
    return "";
}

String styleValue(const String& style, const char* property) {
    int start = 0;
    while (start < static_cast<int>(style.length())) {
        int end = style.indexOf(';', start);
        if (end < 0) end = style.length();
        int colon = style.indexOf(':', start);
        if (colon >= start && colon < end) {
            String name = style.substring(start, colon);
            name.trim();
            name.toLowerCase();
            if (name == property) {
                String value = style.substring(colon + 1, end);
                value.trim();
                return value;
            }
        }
        start = end + 1;
    }
    return "";
}

bool cssDeclarationIsHidden(String declaration) {
    declaration.toLowerCase();
    for (int i = static_cast<int>(declaration.length()) - 1; i >= 0; i--) {
        if (isspace(static_cast<unsigned char>(declaration[i]))) declaration.remove(i, 1);
    }
    return declaration.indexOf("display:none") >= 0 || declaration.indexOf("visibility:hidden") >= 0;
}

bool simpleClassSelector(String selector, String& className) {
    selector.trim();
    selector.toLowerCase();
    if (selector.length() == 0 || selector.indexOf(' ') >= 0 || selector.indexOf('>') >= 0 ||
        selector.indexOf('+') >= 0 || selector.indexOf('~') >= 0 || selector.indexOf('#') >= 0 ||
        selector.indexOf('[') >= 0 || selector.indexOf(':') >= 0) return false;

    int dot = selector.indexOf('.');
    if (dot < 0 || selector.indexOf('.', dot + 1) >= 0) return false;
    className = selector.substring(dot + 1);
    if (className.length() == 0) return false;
    for (size_t i = 0; i < className.length(); i++) {
        char c = className[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return true;
}

void collectHiddenCssClasses(String css, std::vector<String>& hiddenClasses) {
    int comment = css.indexOf("/*");
    while (comment >= 0) {
        int end = css.indexOf("*/", comment + 2);
        if (end < 0) {
            css.remove(comment);
            break;
        }
        css.remove(comment, end + 2 - comment);
        comment = css.indexOf("/*", comment);
    }

    int cursor = 0;
    while (cursor < static_cast<int>(css.length())) {
        int open = css.indexOf('{', cursor);
        if (open < 0) break;
        int close = css.indexOf('}', open + 1);
        if (close < 0) break;
        if (cssDeclarationIsHidden(css.substring(open + 1, close))) {
            String selectors = css.substring(cursor, open);
            int selectorStart = 0;
            while (selectorStart <= static_cast<int>(selectors.length())) {
                int comma = selectors.indexOf(',', selectorStart);
                if (comma < 0) comma = selectors.length();
                String className;
                if (simpleClassSelector(selectors.substring(selectorStart, comma), className)) {
                    bool duplicate = false;
                    for (const String& existing : hiddenClasses) {
                        if (existing == className) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) hiddenClasses.push_back(className);
                }
                if (comma >= static_cast<int>(selectors.length())) break;
                selectorStart = comma + 1;
            }
        }
        cursor = close + 1;
    }
}

void parseImageLength(String value, int& pixels, int& percent) {
    value.trim();
    value.toLowerCase();
    if (value.length() == 0 || value == "auto") return;
    if (value.endsWith("%")) {
        percent = constrain(static_cast<int>(value.toFloat() + 0.5f), 1, 100);
        pixels = 0;
        return;
    }
    if (value.endsWith("px")) value.remove(value.length() - 2);
    for (size_t i = 0; i < value.length(); i++) {
        if (!isdigit(static_cast<unsigned char>(value[i])) && value[i] != '.') return;
    }
    int parsed = static_cast<int>(value.toFloat() + 0.5f);
    if (parsed > 0) {
        pixels = parsed;
        percent = 0;
    }
}

int indentFromStyle(String style) {
    style.toLowerCase();
    int pos = style.indexOf("text-indent");
    if (pos < 0) return 0;
    int colon = style.indexOf(':', pos);
    if (colon < 0) return 0;
    int end = style.indexOf(';', colon + 1);
    if (end < 0) end = style.length();
    String value = style.substring(colon + 1, end);
    value.trim();
    if (value.endsWith("em")) return static_cast<int>(value.toFloat() * 20.0f);
    return value.toInt();
}

TextAlign alignmentFromStyle(String style) {
    style.toLowerCase();
    int pos = style.indexOf("text-align");
    if (pos < 0) return ALIGN_LEFT;
    int colon = style.indexOf(':', pos);
    if (colon < 0) return ALIGN_LEFT;
    int end = style.indexOf(';', colon + 1);
    if (end < 0) end = style.length();
    String value = style.substring(colon + 1, end);
    value.trim();
    if (value.startsWith("center")) return ALIGN_CENTER;
    if (value.startsWith("right")) return ALIGN_RIGHT;
    if (value.startsWith("justify")) return ALIGN_JUSTIFY;
    return ALIGN_LEFT;
}

class RichHtmlStreamParser {
public:
    RichHtmlStreamParser(const String& documentPath, const std::vector<String>& hiddenClasses)
        : _documentPath(documentPath), _hiddenClasses(hiddenClasses) {
        _styles.push_back(STYLE_NORMAL);
        _nodes.reserve(64);
    }

    void feed(const uint8_t* data, size_t length) {
        for (size_t i = 0; i < length; i++) {
            char c = static_cast<char>(data[i]);
            if (_inTag) {
                if (c == '>') {
                    _inTag = false;
                    processTag(_tag);
                    _tag = "";
                } else if (_tag.length() < 1024) {
                    _tag += c;
                }
                continue;
            }

            if (c == '<') {
                flushText();
                _inTag = true;
                _tag = "";
            } else if (_skipDepth == 0 && _svgDepth == 0) {
                appendText(c);
            }
        }
    }

    std::vector<ContentNode> finish() {
        flushText();
        removeSvgFallbackDuplicates();
        return std::move(_nodes);
    }

private:
    std::vector<ContentNode> _nodes;
    std::vector<TextStyle> _styles;
    std::vector<bool> _blockStylePushed;
    String _text;
    String _tag;
    bool _inTag = false;
    bool _nextBlockStart = true;
    bool _listItem = false;
    int _indent = 0;
    TextAlign _align = ALIGN_LEFT;
    int _skipDepth = 0;
    int _svgDepth = 0;
    int _hiddenDepth = 0;
    String _documentPath;
    const std::vector<String>& _hiddenClasses;

    void removeSvgFallbackDuplicates() {
        if (_nodes.size() < 2) return;
        std::vector<ContentNode> filtered;
        filtered.reserve(_nodes.size());
        for (ContentNode& node : _nodes) {
            if (node.type == CONTENT_IMAGE && !filtered.empty() && filtered.back().type == CONTENT_IMAGE &&
                filtered.back().imageNode.fromSvg && !node.imageNode.fromSvg) {
                filtered.back() = std::move(node);
                continue;
            }
            filtered.push_back(std::move(node));
        }
        _nodes = std::move(filtered);
    }

    void appendText(char c) {
        if (isspace(static_cast<unsigned char>(c))) {
            if (_text.length() > 0 && _text[_text.length() - 1] != ' ') _text += ' ';
            return;
        }
        _text += c;
    }

    void flushText() {
        if (_skipDepth > 0 || _text.length() == 0) {
            _text = "";
            return;
        }

        String clean = decodeHtmlEntities(_text);
        clean.replace("¶Ç8", " -- ");
        clean.replace("¶ÇÖ", "'");
        clean.replace("¶Çö", "'");
        clean.replace("¶Ç£", "\"");
        clean.replace("¶Ç¥", "\"");
        clean.replace("¶Ç", " ");
        clean.replace("\xE2\x80\x9C", "\"");
        clean.replace("\xE2\x80\x9D", "\"");
        clean.replace("\xE2\x80\x98", "'");
        clean.replace("\xE2\x80\x99", "'");
        clean.replace("\xE2\x80\x94", " -- ");
        clean.replace("\xE2\x80\x93", " - ");
        clean.replace("\xE2\x80\xA6", "...");
        clean.trim();
        _text = "";
        if (clean.length() == 0 || clean == "Unknown" || clean == "image" || clean == "Image" || clean == "[image]") return;

        ContentNode node;
        node.type = CONTENT_TEXT;
        node.textNode.text = clean;
        node.textNode.style = _styles.back();
        if (_nextBlockStart && clean.length() <= 3) {
            bool numeric = true;
            for (size_t i = 0; i < clean.length(); i++) {
                if (!isdigit(static_cast<unsigned char>(clean[i]))) {
                    numeric = false;
                    break;
                }
            }
            if (numeric) node.textNode.style = STYLE_HEADER1;
        }
        node.textNode.align = _align;
        node.textNode.isListItem = _listItem;
        node.textNode.isBlockStart = _nextBlockStart;
        node.textNode.indent = _indent;
        _nodes.push_back(std::move(node));
        _nextBlockStart = false;
        _listItem = false;
    }

    void pushStyle(TextStyle style) { _styles.push_back(style); }
    void popStyle() { if (_styles.size() > 1) _styles.pop_back(); }

    static bool isSkippedTag(const String& name) {
        return name == "script" || name == "style" || name == "head";
    }

    static bool isVoidTag(const String& name) {
        return name == "area" || name == "base" || name == "br" || name == "col" || name == "embed" ||
               name == "hr" || name == "img" || name == "image" || name == "input" || name == "link" ||
               name == "meta" || name == "param" || name == "source" || name == "track" || name == "wbr";
    }

    bool classIsHidden(String classes) const {
        classes.toLowerCase();
        int start = 0;
        while (start < static_cast<int>(classes.length())) {
            while (start < static_cast<int>(classes.length()) &&
                   isspace(static_cast<unsigned char>(classes[start]))) start++;
            int end = start;
            while (end < static_cast<int>(classes.length()) &&
                   !isspace(static_cast<unsigned char>(classes[end]))) end++;
            String token = classes.substring(start, end);
            for (const String& hiddenClass : _hiddenClasses) {
                if (token == hiddenClass) return true;
            }
            start = end + 1;
        }
        return false;
    }

    bool tagIsHidden(const String& raw) const {
        if (cssDeclarationIsHidden(attributeValue(raw, "style"))) return true;
        if (classIsHidden(attributeValue(raw, "class"))) return true;
        String ariaHidden = attributeValue(raw, "aria-hidden");
        ariaHidden.trim();
        ariaHidden.toLowerCase();
        return ariaHidden == "true";
    }

    void processTag(String raw) {
        raw.trim();
        if (raw.length() == 0 || raw.startsWith("!") || raw.startsWith("?")) return;

        bool closing = raw.startsWith("/");
        if (closing) {
            raw.remove(0, 1);
            raw.trim();
        }
        bool selfClosing = raw.endsWith("/");

        int nameEnd = 0;
        while (nameEnd < static_cast<int>(raw.length()) && !isspace(static_cast<unsigned char>(raw[nameEnd])) && raw[nameEnd] != '/') nameEnd++;
        String name = raw.substring(0, nameEnd);
        name.toLowerCase();
        if (name.length() == 0) return;

        if (_hiddenDepth > 0) {
            if (closing) _hiddenDepth = max(0, _hiddenDepth - 1);
            else if (!selfClosing && !isVoidTag(name)) _hiddenDepth++;
            return;
        }
        if (!closing && tagIsHidden(raw)) {
            if (!selfClosing && !isVoidTag(name)) _hiddenDepth = 1;
            return;
        }

        if (_skipDepth > 0) {
            if (!closing && isSkippedTag(name) && !selfClosing) _skipDepth++;
            else if (closing && isSkippedTag(name)) _skipDepth--;
            return;
        }
        if (!closing && isSkippedTag(name)) {
            if (!selfClosing) _skipDepth = 1;
            return;
        }

        if (name == "svg") {
            if (closing) _svgDepth = max(0, _svgDepth - 1);
            else if (!selfClosing) _svgDepth++;
            _nextBlockStart = true;
            return;
        }

        if (name == "img" || name == "image") {
            if (closing) return;
            String source = attributeValue(raw, "src");
            if (source.length() == 0) source = attributeValue(raw, "xlink:href");
            if (source.length() == 0) source = attributeValue(raw, "href");
            source = resolveZipHref(_documentPath, source);
            if (source.length() == 0) return;

            ContentNode node;
            node.type = CONTENT_IMAGE;
            node.imageNode.href = source;
            node.imageNode.alt = decodeHtmlEntities(attributeValue(raw, "alt"));
            node.imageNode.fromSvg = _svgDepth > 0;
            parseImageLength(attributeValue(raw, "width"), node.imageNode.requestedWidth,
                             node.imageNode.widthPercent);
            parseImageLength(attributeValue(raw, "height"), node.imageNode.requestedHeight,
                             node.imageNode.heightPercent);
            String style = attributeValue(raw, "style");
            String styleWidth = styleValue(style, "width");
            String styleHeight = styleValue(style, "height");
            if (styleWidth.length() > 0) {
                node.imageNode.requestedWidth = 0;
                node.imageNode.widthPercent = 0;
                parseImageLength(styleWidth, node.imageNode.requestedWidth, node.imageNode.widthPercent);
            }
            if (styleHeight.length() > 0) {
                node.imageNode.requestedHeight = 0;
                node.imageNode.heightPercent = 0;
                parseImageLength(styleHeight, node.imageNode.requestedHeight, node.imageNode.heightPercent);
            }
            _nodes.push_back(std::move(node));
            _nextBlockStart = true;
            _listItem = false;
            return;
        }

        if (name == "br") {
            _nextBlockStart = true;
            return;
        }
        if (name == "b" || name == "strong") {
            if (closing) popStyle(); else pushStyle(STYLE_BOLD);
            return;
        }
        if (name == "i" || name == "em") {
            if (closing) popStyle(); else pushStyle(STYLE_ITALIC);
            return;
        }

        if (name.length() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
            if (closing) {
                popStyle();
                _nextBlockStart = true;
            } else {
                int level = name[1] - '0';
                pushStyle(level == 1 ? STYLE_HEADER1 : level == 2 ? STYLE_HEADER2 : level == 3 ? STYLE_HEADER3 : STYLE_HEADER4);
                _nextBlockStart = true;
            }
            return;
        }

        bool block = name == "p" || name == "div" || name == "section" || name == "article" ||
                     name == "blockquote" || name == "figure" || name == "figcaption";
        if (block) {
            if (closing) {
                if (!_blockStylePushed.empty()) {
                    if (_blockStylePushed.back()) popStyle();
                    _blockStylePushed.pop_back();
                }
                _align = ALIGN_LEFT;
                _indent = 0;
                _nextBlockStart = true;
            } else {
                String style = attributeValue(raw, "style");
                _align = alignmentFromStyle(style);
                _indent = indentFromStyle(style);
                if (name == "p" && _indent == 0 && _styles.back() == STYLE_NORMAL) _indent = 30;

                String className = attributeValue(raw, "class");
                className.toLowerCase();
                bool titleClass = className.indexOf("chapter-title") >= 0 || className.indexOf("chap-title") >= 0 ||
                                  className.indexOf("section-title") >= 0 || className.indexOf("part-title") >= 0;
                _blockStylePushed.push_back(titleClass);
                if (titleClass) pushStyle(STYLE_HEADER1);
                _nextBlockStart = true;
            }
            return;
        }

        if (name == "li") {
            _listItem = !closing;
            _nextBlockStart = true;
            return;
        }
        if (name == "tr") {
            _nextBlockStart = true;
            return;
        }
    }
};

}  // namespace

void *myOpen(const char *filename, int32_t *size) {
    if (zipFd >= 0) { close(zipFd); zipFd = -1; }
    String fullPath = filename;
    if (!fullPath.startsWith("/littlefs") && !fullPath.startsWith("/ebooks")) fullPath = "/littlefs" + fullPath;
    zipFd = open(fullPath.c_str(), O_RDONLY);
    if (zipFd < 0) return NULL;
    struct stat st;
    if (fstat(zipFd, &st) != 0) { close(zipFd); zipFd = -1; return NULL; }
    *size = st.st_size;
    return (void*)(intptr_t)(zipFd + 1);
}

void myClose(void *p) { if (zipFd >= 0) { close(zipFd); zipFd = -1; } }
int32_t myRead(void *p, uint8_t *buffer, int32_t length) {
    if (zipFd < 0 || !buffer || length <= 0) return -1;
    return (int32_t)read(zipFd, buffer, length);
}
int32_t mySeek(void *p, int32_t position, int iType) {
    if (zipFd < 0) return -1;
    return (int32_t)lseek(zipFd, position, iType);
}

EpubLoader::EpubLoader() {
    void* storage = ps_malloc(sizeof(UNZIP));
    if (!storage) storage = malloc(sizeof(UNZIP));
    zip = storage ? new (storage) UNZIP() : nullptr;
}

EpubLoader::~EpubLoader() { if (zip) { zip->~UNZIP(); free(zip); zip = nullptr; } }

bool EpubLoader::open(const char* path) {
    if (!zip || !path) return false;
    spine.clear();
    toc.clear();
    manifest.clear();
    fonts.clear();
    epubPath = String(path);
    coverHref = "";
    hiddenCssClasses.clear();
    if (zip->openZIP(path, myOpen, myClose, myRead, mySeek) != ZIP_SUCCESS) return false;
    if (!parseContainer()) { close(); return false; }
    if (!parseOpf()) { close(); return false; }
    return true;
}

void EpubLoader::close() {
    if (zip) zip->closeZIP();
    if (zipFd >= 0) { ::close(zipFd); zipFd = -1; }
    spine.clear(); toc.clear(); manifest.clear(); fonts.clear(); hiddenCssClasses.clear(); coverHref = "";
}

String EpubLoader::getTitle() { return bookTitle; }
int EpubLoader::getChapterCount() { return spine.size(); }

String EpubLoader::getChapterTitle(int index) const {
    if (index < 0 || index >= static_cast<int>(spine.size())) return "";
    const int tocIndex = spine[index].tocIndex;
    if (tocIndex >= 0 && tocIndex < static_cast<int>(toc.size()) && toc[tocIndex].title.length() > 0) {
        return toc[tocIndex].title;
    }
    return "Chapter " + String(index + 1);
}

float EpubLoader::calculateBookProgress(int index, float chapterProgress) const {
    if (spine.empty()) return 0.0f;
    index = constrain(index, 0, static_cast<int>(spine.size()) - 1);
    chapterProgress = constrain(chapterProgress, 0.0f, 1.0f);

    uint64_t totalSize = 0;
    uint64_t previousSize = 0;
    for (int i = 0; i < static_cast<int>(spine.size()); i++) {
        totalSize += spine[i].uncompressedSize;
        if (i < index) previousSize += spine[i].uncompressedSize;
    }
    if (totalSize == 0) {
        return (index + chapterProgress) / static_cast<float>(spine.size());
    }
    const float currentSize = chapterProgress * static_cast<float>(spine[index].uncompressedSize);
    return (static_cast<float>(previousSize) + currentSize) / static_cast<float>(totalSize);
}

String EpubLoader::getChapterContent(int index) {
    if(index < 0 || index >= (int)spine.size()) return "";
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    String content = readFileFromZip(fullPath.c_str());
    if (content.length() == 0) return "";
    
    // --- ADVANCED HTML PARSING ---
    String clean;
    clean.reserve(content.length());
    bool inTag = false, skipContent = false;
    String currentTag;
    
    for(int i = 0; i < (int)content.length(); i++) {
        char c = content.charAt(i);
        if(c == '<') {
            inTag = true; currentTag = ""; int j = i + 1;
            while(j < (int)content.length() && content.charAt(j) != '>' && content.charAt(j) != ' ' && j - i < 20) { currentTag += (char)tolower(content.charAt(j)); j++; }
            
            // BLOCK ELEMENTS: cause a newline
            if(currentTag == "p" || currentTag == "/p" || currentTag == "div" || currentTag == "/div" || currentTag == "br" || currentTag == "br/" || currentTag.startsWith("h")) {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') clean += "\n";
            }
            else if(currentTag == "li") {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') clean += "\n";
                clean += "• ";
            }
            // Skip image/media elements completely
            else if(currentTag == "img" || currentTag == "svg" || currentTag == "figure" || currentTag == "image") {
                // Skip - these are self-closing or we don't want their content
            }
            else if(currentTag == "/figure" || currentTag == "/svg") {
                // End of skipped elements
            }
            else if(currentTag == "script" || currentTag == "style" || currentTag == "head") skipContent = true;
            else if(currentTag == "/script" || currentTag == "/style" || currentTag == "/head") skipContent = false;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag && !skipContent) {
            if(c == '\n' || c == '\r' || c == '\t') c = ' ';
            clean += c;
        }
    }

    // --- AGGRESSIVE CLEANING ---
    // Handle Windows-1252 / UTF-8 mix-up artifacts seen in Sanderson EPUBs
    clean.replace("¶Ç8", " -- ");
    clean.replace("¶ÇÖ", "'");
    clean.replace("¶Çö", "'");
    clean.replace("¶Ç£", "\"");
    clean.replace("¶Ç¥", "\"");
    clean.replace("¶Çª", "-");
    clean.replace("¶ÇÜ", "...");
    clean.replace("¶Ç", ""); // Wipe any remaining prefix
    
    // Standard UTF-8
    clean.replace("\xE2\x80\x9C", "\""); clean.replace("\xE2\x80\x9D", "\"");
    clean.replace("\xE2\x80\x98", "'"); clean.replace("\xE2\x80\x99", "'");
    clean.replace("\xE2\x80\x94", " -- "); clean.replace("\xE2\x80\x93", " - ");
    clean.replace("\xE2\x80\xA6", "...");
    
    // Strip accidental newlines before punctuation (fixes the orphan comma/dot)
    clean.replace("\n,", ",");
    clean.replace("\n.", ".");
    clean.replace("\n?", "?");
    clean.replace("\n!", "!");
    clean.replace("\n\"", "\"");
    clean.replace("\n'", "'");
    
    // Collapse multiple spaces
    while(clean.indexOf("  ") != -1) clean.replace("  ", " ");
    
    clean.trim();
    return clean;
}

bool EpubLoader::parseContainer() {
    String xml = readFileFromZip("META-INF/container.xml");
    if(xml.length() == 0) return false;
    opfPath = extractAttribute(xml, "rootfile", "full-path");
    if(opfPath.length() == 0) return false;
    int lastSlash = opfPath.lastIndexOf('/');
    if(lastSlash != -1) rootDir = opfPath.substring(0, lastSlash + 1);
    else rootDir = "";
    return true;
}

bool EpubLoader::parseOpf() {
    String xml = readFileFromZip(opfPath.c_str());
    if(xml.length() == 0) return false;
    bookTitle = extractMetadata(xml, "dc:title");
    if(bookTitle.length() == 0) bookTitle = extractMetadata(xml, "title");

    String coverId;
    int metaPosition = 0;
    while (true) {
        int metaStart = xml.indexOf("<meta", metaPosition);
        if (metaStart < 0) break;
        int metaEnd = xml.indexOf('>', metaStart);
        if (metaEnd < 0) break;
        String metaTag = xml.substring(metaStart, metaEnd + 1);
        String name = extractAttribute(metaTag, "meta", "name");
        name.toLowerCase();
        if (name == "cover") {
            coverId = extractAttribute(metaTag, "meta", "content");
            if (coverId.length() > 0) break;
        }
        metaPosition = metaEnd + 1;
    }
    int manifestStart = xml.indexOf("<manifest");
    int manifestEnd = xml.indexOf("</manifest>");
    if(manifestStart == -1 || manifestEnd == -1) return false;

    String manifestBlock = xml.substring(manifestStart, manifestEnd);
    String probableCoverHref;
    String coverWrapperHref;
    String navPath;
    String ncxPath;
    std::vector<String> stylesheetPaths;
    int pos = 0;
    while(true) {
        int itemStart = manifestBlock.indexOf("<item", pos);
        if(itemStart == -1) break;
        int itemEnd = manifestBlock.indexOf(">", itemStart);
        String itemTag = manifestBlock.substring(itemStart, itemEnd+1);
        String id = extractAttribute(itemTag, "item", "id");
        String href = extractAttribute(itemTag, "item", "href");
        String mediaType = extractAttribute(itemTag, "item", "media-type");
        String properties = extractAttribute(itemTag, "item", "properties");
        if(id.length() > 0 && href.length() > 0) {
            manifest[id] = href;
            String hrefLower = href; hrefLower.toLowerCase();
            String idLower = id; idLower.toLowerCase();
            String mediaLower = mediaType; mediaLower.toLowerCase();
            String propertiesLower = properties; propertiesLower.toLowerCase();
            if (propertiesLower.indexOf("nav") >= 0) navPath = resolveZipHref(opfPath, href);
            if (mediaLower == "application/x-dtbncx+xml" || hrefLower.endsWith(".ncx")) {
                ncxPath = resolveZipHref(opfPath, href);
            }
            if (mediaLower == "text/css" || hrefLower.endsWith(".css")) {
                stylesheetPaths.push_back(resolveZipHref(opfPath, href));
            }
            bool supportedRaster = mediaLower == "image/jpeg" || mediaLower == "image/png" ||
                                   hrefLower.endsWith(".jpg") || hrefLower.endsWith(".jpeg") ||
                                   hrefLower.endsWith(".png");
            if (supportedRaster) {
                String resolved = resolveZipHref(opfPath, href);
                if (propertiesLower.indexOf("cover-image") >= 0 || id == coverId) {
                    coverHref = resolved;
                } else if (probableCoverHref.length() == 0 &&
                           (idLower.indexOf("cover") >= 0 || hrefLower.indexOf("cover") >= 0)) {
                    probableCoverHref = resolved;
                }
            } else if (id == coverId || propertiesLower.indexOf("cover-image") >= 0) {
                coverWrapperHref = resolveZipHref(opfPath, href);
            }
            if(hrefLower.endsWith(".ttf") || hrefLower.endsWith(".otf") || mediaType.indexOf("font") != -1) {
                FontInfo font; font.path = rootDir + href;
                if(hrefLower.endsWith(".ttf")) font.format = "ttf";
                else if(hrefLower.endsWith(".otf")) font.format = "otf";
                int lastSlash = href.lastIndexOf('/'), lastDot = href.lastIndexOf('.');
                if(lastSlash != -1 && lastDot != -1) font.family = href.substring(lastSlash + 1, lastDot);
                else if(lastDot != -1) font.family = href.substring(0, lastDot);
                String fLower = font.family; fLower.toLowerCase();
                if(fLower.indexOf("bolditalic") != -1) font.style = "bold-italic";
                else if(fLower.indexOf("bold") != -1) font.style = "bold";
                else if(fLower.indexOf("italic") != -1) font.style = "italic";
                else font.style = "normal";
                fonts.push_back(font);
            }
        }
        pos = itemEnd;
    }
    for (const String& stylesheetPath : stylesheetPaths) {
        String css = readFileFromZip(stylesheetPath.c_str());
        if (css.length() > 0) collectHiddenCssClasses(std::move(css), hiddenCssClasses);
    }
    if (coverHref.length() == 0 && coverWrapperHref.length() > 0) {
        String wrapper = readFileFromZip(coverWrapperHref.c_str());
        String source = extractAttribute(wrapper, "img", "src");
        if (source.length() == 0) source = extractAttribute(wrapper, "image", "xlink:href");
        if (source.length() == 0) source = extractAttribute(wrapper, "image", "href");
        coverHref = resolveZipHref(coverWrapperHref, source);
    }
    if (coverHref.length() == 0) coverHref = probableCoverHref;

    if (coverHref.length() == 0) {
        int guideStart = xml.indexOf("<guide");
        int guideEnd = guideStart >= 0 ? xml.indexOf("</guide>", guideStart) : -1;
        int referencePosition = guideStart;
        while (guideStart >= 0 && guideEnd > guideStart) {
            int referenceStart = xml.indexOf("<reference", referencePosition);
            if (referenceStart < 0 || referenceStart >= guideEnd) break;
            int referenceEnd = xml.indexOf('>', referenceStart);
            if (referenceEnd < 0 || referenceEnd >= guideEnd) break;
            String reference = xml.substring(referenceStart, referenceEnd + 1);
            String type = extractAttribute(reference, "reference", "type");
            type.toLowerCase();
            if (type == "cover") {
                String wrapperHref = resolveZipHref(opfPath, extractAttribute(reference, "reference", "href"));
                String lowerHref = wrapperHref;
                lowerHref.toLowerCase();
                if (lowerHref.endsWith(".jpg") || lowerHref.endsWith(".jpeg") || lowerHref.endsWith(".png")) {
                    coverHref = wrapperHref;
                } else if (wrapperHref.length() > 0) {
                    String wrapper = readFileFromZip(wrapperHref.c_str());
                    String source = extractAttribute(wrapper, "img", "src");
                    if (source.length() == 0) source = extractAttribute(wrapper, "image", "xlink:href");
                    if (source.length() == 0) source = extractAttribute(wrapper, "image", "href");
                    coverHref = resolveZipHref(wrapperHref, source);
                }
                break;
            }
            referencePosition = referenceEnd + 1;
        }
    }
    int spineStart = xml.indexOf("<spine"), spineEnd = xml.indexOf("</spine>");
    if(spineStart == -1 || spineEnd == -1) return false;
    String spineBlock = xml.substring(spineStart, spineEnd);
    pos = 0;
    while(true) {
        int itemRefStart = spineBlock.indexOf("<itemref", pos);
        if(itemRefStart == -1) break;
        int itemRefEnd = spineBlock.indexOf(">", itemRefStart);
        String itemRefTag = spineBlock.substring(itemRefStart, itemRefEnd+1);
        String idref = extractAttribute(itemRefTag, "itemref", "idref");
        if(idref.length() > 0 && manifest.count(idref)) {
            SpineItem item; item.id = idref; item.href = manifest[idref];
            item.uncompressedSize = getFileSizeFromZip(resolveZipHref(opfPath, item.href));
            spine.push_back(item);
        }
        pos = itemRefEnd;
    }

    // EPUB reading order (the spine) commonly contains cover, title, copyright,
    // and other files that are not chapters. Map it to the EPUB 2/3 navigation
    // document so the reader reports the chapter the publisher intended.
    parseToc(navPath, ncxPath);
    mapTocToSpine();

    return true;
}

void EpubLoader::parseToc(const String& navPath, const String& ncxPath) {
    toc.clear();
    String documentPath;
    String xml;
    bool isNav = false;

    if (navPath.length() > 0) {
        xml = readFileFromZip(navPath.c_str());
        documentPath = navPath;
        isNav = xml.length() > 0;
    }
    if (xml.length() == 0 && ncxPath.length() > 0) {
        xml = readFileFromZip(ncxPath.c_str());
        documentPath = ncxPath;
        isNav = false;
    }
    if (xml.length() == 0) return;

    if (isNav) {
        String lower = xml;
        lower.toLowerCase();
        int navStart = 0;
        while ((navStart = lower.indexOf("<nav", navStart)) >= 0) {
            const int tagEnd = lower.indexOf('>', navStart);
            if (tagEnd < 0) break;
            String tag = lower.substring(navStart, tagEnd + 1);
            if (tag.indexOf("epub:type=\"toc\"") >= 0 || tag.indexOf("epub:type='toc'") >= 0 ||
                tag.indexOf("type=\"toc\"") >= 0 || tag.indexOf("type='toc'") >= 0) {
                const int navEnd = lower.indexOf("</nav>", tagEnd);
                const int limit = navEnd >= 0 ? navEnd : static_cast<int>(xml.length());
                int pos = tagEnd + 1;
                while ((pos = lower.indexOf("<a", pos)) >= 0 && pos < limit) {
                    const int anchorTagEnd = lower.indexOf('>', pos);
                    const int anchorEnd = lower.indexOf("</a>", anchorTagEnd);
                    if (anchorTagEnd < 0 || anchorEnd < 0 || anchorEnd > limit) break;
                    const String anchorTag = xml.substring(pos, anchorTagEnd + 1);
                    const String rawHref = attributeValue(anchorTag, "href");
                    const String title = stripMarkup(xml.substring(anchorTagEnd + 1, anchorEnd));
                    const String href = resolveZipHref(documentPath, rawHref);
                    if (title.length() > 0 && href.length() > 0) {
                        TocEntry entry;
                        entry.title = title;
                        entry.href = href;
                        toc.push_back(entry);
                    }
                    pos = anchorEnd + 4;
                }
                break;
            }
            navStart = tagEnd + 1;
        }
    } else {
        String lower = xml;
        lower.toLowerCase();
        int pos = 0;
        while ((pos = lower.indexOf("<navpoint", pos)) >= 0) {
            const int pointEnd = lower.indexOf("</navpoint>", pos);
            const int limit = pointEnd >= 0 ? pointEnd : static_cast<int>(xml.length());
            const int textStart = lower.indexOf("<text", pos);
            const int contentStart = lower.indexOf("<content", pos);
            if (textStart >= 0 && textStart < limit && contentStart >= 0 && contentStart < limit) {
                const int textTagEnd = lower.indexOf('>', textStart);
                const int textEnd = lower.indexOf("</text>", textTagEnd);
                const int contentEnd = lower.indexOf('>', contentStart);
                if (textTagEnd >= 0 && textEnd >= 0 && textEnd < limit && contentEnd >= 0) {
                    const String title = stripMarkup(xml.substring(textTagEnd + 1, textEnd));
                    const String contentTag = xml.substring(contentStart, contentEnd + 1);
                    const String href = resolveZipHref(documentPath, attributeValue(contentTag, "src"));
                    if (title.length() > 0 && href.length() > 0) {
                        TocEntry entry;
                        entry.title = title;
                        entry.href = href;
                        toc.push_back(entry);
                    }
                }
            }
            pos += 9;
        }
    }

    // Some EPUB 3 books advertise a nav document that is malformed or does
    // not actually contain a TOC. Fall back to the EPUB 2 NCX when available.
    if (toc.empty() && isNav && ncxPath.length() > 0) parseToc("", ncxPath);
}

void EpubLoader::mapTocToSpine() {
    for (int tocIndex = 0; tocIndex < static_cast<int>(toc.size()); tocIndex++) {
        for (int spineIndex = 0; spineIndex < static_cast<int>(spine.size()); spineIndex++) {
            const String spineHref = resolveZipHref(opfPath, spine[spineIndex].href);
            if (spineHref == toc[tocIndex].href) {
                toc[tocIndex].spineIndex = spineIndex;
                if (spine[spineIndex].tocIndex < 0) spine[spineIndex].tocIndex = tocIndex;
                break;
            }
        }
    }

    int lastTocIndex = -1;
    for (SpineItem& item : spine) {
        if (item.tocIndex >= 0) lastTocIndex = item.tocIndex;
        else item.tocIndex = lastTocIndex;
    }
}

uint8_t* EpubLoader::getFontData(String path, size_t* outSize) {
    if(path.length() == 0) return nullptr;
    if (zip->locateFile(path.c_str()) != 0) return nullptr;
    if (zip->openCurrentFile() != 0) return nullptr;
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;
    uint8_t* buffer = (uint8_t*)ps_malloc(size);
    if(!buffer) buffer = (uint8_t*)malloc(size);
    if(!buffer) { zip->closeCurrentFile(); return nullptr; }
    zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();
    *outSize = size;
    return buffer;
}

String EpubLoader::extractAttribute(const String& xml, const String& tag, const String& attr) {
    int attrStart = xml.indexOf(attr + "=\"");
    if(attrStart == -1) attrStart = xml.indexOf(attr + "='");
    if(attrStart == -1) return "";
    int valStart = attrStart + attr.length() + 2; 
    char quote = xml.charAt(attrStart + attr.length() + 1); 
    int valEnd = xml.indexOf(quote, valStart);
    if(valEnd == -1) return "";
    return xml.substring(valStart, valEnd);
}

String EpubLoader::extractMetadata(const String& xml, const String& tag) {
    int tagStart = xml.indexOf("<" + tag);
    if(tagStart == -1) return "";
    int tagEnd = xml.indexOf(">", tagStart);
    if(tagEnd == -1) return "";
    int contentEnd = xml.indexOf("</" + tag + ">", tagEnd);
    if(contentEnd == -1) contentEnd = xml.indexOf("</", tagEnd);
    if(contentEnd == -1) return "";
    String content = xml.substring(tagEnd + 1, contentEnd);
    content.trim();
    return content;
}

String EpubLoader::readFileFromZip(const char* path) {
    if (zip->locateFile(path) != ZIP_SUCCESS) return "";
    if (zip->openCurrentFile() != ZIP_SUCCESS) return "";
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    int size = fileInfo.uncompressed_size;

    String str;
    str.reserve(size + 1);
    char buffer[513];
    int remaining = size;
    while (remaining > 0) {
        int toRead = remaining > 512 ? 512 : remaining;
        int bytesRead = zip->readCurrentFile((uint8_t*)buffer, toRead);
        if (bytesRead <= 0) break;
        buffer[bytesRead] = '\0';
        str += buffer;
        remaining -= bytesRead;
        yield();
    }

    zip->closeCurrentFile();
    return str;
}

uint32_t EpubLoader::getFileSizeFromZip(const String& path) {
    if (!zip || path.length() == 0 || zip->locateFile(path.c_str()) != ZIP_SUCCESS) return 0;
    if (zip->openCurrentFile() != ZIP_SUCCESS) return 0;
    unz_file_info fileInfo = {};
    char fileName[256];
    const int result = zip->getFileInfo(&fileInfo, fileName, sizeof(fileName), nullptr, 0, nullptr, 0);
    zip->closeCurrentFile();
    return result == ZIP_SUCCESS ? fileInfo.uncompressed_size : 0;
}

uint8_t* EpubLoader::readItemBytes(const String& path, size_t& size, size_t maximumBytes) {
    size = 0;
    if (!zip || path.length() == 0 || zip->locateFile(path.c_str()) != ZIP_SUCCESS) return nullptr;
    if (zip->openCurrentFile() != ZIP_SUCCESS) return nullptr;

    unz_file_info fileInfo = {};
    char fileName[256];
    if (zip->getFileInfo(&fileInfo, fileName, sizeof(fileName), nullptr, 0, nullptr, 0) != ZIP_SUCCESS ||
        fileInfo.uncompressed_size == 0 || fileInfo.uncompressed_size > maximumBytes) {
        zip->closeCurrentFile();
        return nullptr;
    }

    size_t required = fileInfo.uncompressed_size;
    size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (required + (512 * 1024) > freePsram) {
        Serial.printf("Epub image skipped: %u bytes needs more free PSRAM\n", static_cast<unsigned>(required));
        zip->closeCurrentFile();
        return nullptr;
    }

    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) {
        zip->closeCurrentFile();
        return nullptr;
    }

    size_t total = 0;
    while (total < required) {
        int chunk = static_cast<int>(min(static_cast<size_t>(4096), required - total));
        int count = zip->readCurrentFile(data + total, chunk);
        if (count <= 0) break;
        total += count;
        yield();
    }
    zip->closeCurrentFile();
    if (total != required) {
        free(data);
        return nullptr;
    }
    size = total;
    return data;
}

uint8_t* EpubLoader::readItemPrefix(const String& path, size_t& size, size_t maximumBytes) {
    size = 0;
    if (!zip || path.length() == 0 || zip->locateFile(path.c_str()) != ZIP_SUCCESS) return nullptr;
    if (zip->openCurrentFile() != ZIP_SUCCESS) return nullptr;

    unz_file_info fileInfo = {};
    char fileName[256];
    if (zip->getFileInfo(&fileInfo, fileName, sizeof(fileName), nullptr, 0, nullptr, 0) != ZIP_SUCCESS ||
        fileInfo.uncompressed_size == 0) {
        zip->closeCurrentFile();
        return nullptr;
    }

    size_t requested = min(static_cast<size_t>(fileInfo.uncompressed_size), maximumBytes);
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(requested, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) data = static_cast<uint8_t*>(malloc(requested));
    if (!data) {
        zip->closeCurrentFile();
        return nullptr;
    }

    size_t total = 0;
    while (total < requested) {
        int chunk = static_cast<int>(min(static_cast<size_t>(2048), requested - total));
        int count = zip->readCurrentFile(data + total, chunk);
        if (count <= 0) break;
        total += count;
    }
    zip->closeCurrentFile();
    if (total == 0) {
        free(data);
        return nullptr;
    }
    size = total;
    return data;
}

bool EpubLoader::getImageDimensions(const String& href, EpubImageInfo& info) {
    size_t size = 0;
    uint8_t* prefix = readItemPrefix(href, size);
    if (!prefix) return false;
    bool valid = EpubImageDecoder::dimensions(prefix, size, info);
    free(prefix);
    return valid;
}

bool EpubLoader::decodeImage(const String& href, int maxWidth, int maxHeight, EpubBitmap& bitmap) {
    size_t size = 0;
    uint8_t* data = readItemBytes(href, size);
    if (!data) return false;
    unsigned long started = millis();
    bool decoded = EpubImageDecoder::decode(data, size, maxWidth, maxHeight, bitmap);
    free(data);
    Serial.printf("EPUB image %s: %s in %lu ms (%dx%d)\n", href.c_str(), decoded ? "decoded" : "failed",
                  millis() - started, bitmap.width, bitmap.height);
    return decoded;
}

std::vector<ContentNode> EpubLoader::readRichContentFromZip(const char* path) {
    if (zip->locateFile(path) != ZIP_SUCCESS) return {};
    if (zip->openCurrentFile() != ZIP_SUCCESS) return {};

    RichHtmlStreamParser parser(path, hiddenCssClasses);
    uint8_t buffer[1024];
    while (true) {
        int bytesRead = zip->readCurrentFile(buffer, sizeof(buffer));
        if (bytesRead <= 0) break;
        parser.feed(buffer, static_cast<size_t>(bytesRead));
        yield();
    }
    zip->closeCurrentFile();
    std::vector<ContentNode> content = parser.finish();
    for (ContentNode& node : content) {
        if (node.type != CONTENT_IMAGE) continue;
        EpubImageInfo info;
        if (getImageDimensions(node.imageNode.href, info)) {
            node.imageNode.sourceWidth = info.width;
            node.imageNode.sourceHeight = info.height;
        }
        if (node.imageNode.sourceWidth <= 0) node.imageNode.sourceWidth = 320;
        if (node.imageNode.sourceHeight <= 0) node.imageNode.sourceHeight = 240;
    }
    return content;
}

String EpubLoader::getAuthor() { return bookAuthor; }
String EpubLoader::getPublisher() { return bookPublisher; }
String EpubLoader::getLanguage() { return bookLanguage; }
String EpubLoader::getPublicationDate() { return bookPubDate; }
String EpubLoader::getISBN() { return bookISBN; }
std::vector<FontInfo> EpubLoader::getFonts() { return fonts; }

TextStyle EpubLoader::getStyleFromTag(String tag) {
    tag.toLowerCase();
    if(tag == "b" || tag == "strong") return STYLE_BOLD;
    if(tag == "i" || tag == "em") return STYLE_ITALIC;
    if(tag == "h1") return STYLE_HEADER1;
    if(tag == "h2") return STYLE_HEADER2;
    if(tag == "h3") return STYLE_HEADER3;
    if(tag == "h4") return STYLE_HEADER4;
    return STYLE_NORMAL;
}

TextAlign EpubLoader::getAlignFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    if(styleAttr.indexOf("text-align:center") != -1 || styleAttr.indexOf("text-align: center") != -1) return ALIGN_CENTER;
    if(styleAttr.indexOf("text-align:right") != -1 || styleAttr.indexOf("text-align: right") != -1) return ALIGN_RIGHT;
    if(styleAttr.indexOf("text-align:justify") != -1 || styleAttr.indexOf("text-align: justify") != -1) return ALIGN_JUSTIFY;
    return ALIGN_LEFT;
}

Table EpubLoader::parseTable(const String& tableHtml) {
    Table table;
    int trPos = 0;
    while(true) {
        int trStart = tableHtml.indexOf("<tr", trPos);
        if(trStart == -1) break;
        int trEnd = tableHtml.indexOf("</tr>", trStart);
        if(trEnd == -1) break;
        String rowHtml = tableHtml.substring(trStart, trEnd + 5);
        TableRow row;
        int cellPos = 0;
        while(true) {
            int tdStart = rowHtml.indexOf("<td", cellPos);
            int thStart = rowHtml.indexOf("<th", cellPos);
            int cellStart = -1;
            bool isHeader = false;
            if(tdStart != -1 && (thStart == -1 || tdStart < thStart)) { cellStart = tdStart; isHeader = false; }
            else if(thStart != -1) { cellStart = thStart; isHeader = true; }
            if(cellStart == -1) break;
            String cellTag = isHeader ? "th" : "td";
            int cellTagEnd = rowHtml.indexOf(">", cellStart);
            int cellEnd = rowHtml.indexOf("</" + cellTag + ">", cellTagEnd);
            if(cellTagEnd == -1 || cellEnd == -1) break;
            TableCell cell;
            cell.isHeader = isHeader;
            String cellOpenTag = rowHtml.substring(cellStart, cellTagEnd + 1);
            String colspanStr = extractAttribute(cellOpenTag, cellTag, "colspan");
            String rowspanStr = extractAttribute(cellOpenTag, cellTag, "rowspan");
            if(colspanStr.length() > 0) cell.colspan = colspanStr.toInt();
            if(rowspanStr.length() > 0) cell.rowspan = rowspanStr.toInt();
            String cellContent = rowHtml.substring(cellTagEnd + 1, cellEnd);
            String clean;
            bool inTag = false;
            for(int i = 0; i < (int)cellContent.length(); i++) {
                char c = cellContent.charAt(i);
                if(c == '<') inTag = true;
                else if(c == '>') inTag = false;
                else if(!inTag) clean += c;
            }
            clean.trim();
            cell.content = clean;
            row.cells.push_back(cell);
            cellPos = cellEnd + cellTag.length() + 3;
        }
        if(row.cells.size() > 0) {
            table.rows.push_back(row);
            if((int)row.cells.size() > table.columnCount) table.columnCount = row.cells.size();
        }
        trPos = trEnd + 5;
    }
    return table;
}

int extractIndentFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    int indentPos = styleAttr.indexOf("text-indent:");
    if (indentPos == -1) indentPos = styleAttr.indexOf("text-indent :");
    if (indentPos != -1) {
        int valStart = styleAttr.indexOf(':', indentPos) + 1;
        int valEnd = styleAttr.indexOf(';', valStart);
        if (valEnd == -1) valEnd = styleAttr.length();
        String val = styleAttr.substring(valStart, valEnd);
        val.trim();
        // Handle em, px, %
        if (val.endsWith("em")) return val.substring(0, val.length()-2).toInt() * 20; // Rough 1em = 20px
        if (val.endsWith("px")) return val.substring(0, val.length()-2).toInt();
        return val.toInt();
    }
    return 0;
}

std::vector<ContentNode> EpubLoader::parseHtmlToRichContent(const String& html) {
    std::vector<ContentNode> nodes;
    std::vector<TextStyle> styleStack;
    styleStack.push_back(STYLE_NORMAL);
    TextAlign currentAlign = ALIGN_LEFT;
    int currentIndent = 0;
    bool isListItem = false;
    bool nextIsBlockStart = true;
    String currentText;
    int i = 0;
    while(i < (int)html.length()) {
        char c = html.charAt(i);
        if(c == '<') {
            if(currentText.length() > 0) {
                ContentNode node;
                node.type = CONTENT_TEXT;
                node.textNode.text = currentText;
                node.textNode.style = styleStack.back();
                node.textNode.align = currentAlign;
                node.textNode.isListItem = isListItem;
                node.textNode.indent = currentIndent;
                node.textNode.isBlockStart = nextIsBlockStart;
                nodes.push_back(node);
                currentText = "";
                isListItem = false;
                currentIndent = 0;
                nextIsBlockStart = false; // Next node in same block is not a start
            }
            int tagEnd = html.indexOf('>', i);
            if(tagEnd == -1) break;
            String fullTag = html.substring(i, tagEnd + 1);
            String tag;
            int spacePos = fullTag.indexOf(' ');
            int closePos = fullTag.indexOf('>');
            if(spacePos != -1 && spacePos < closePos) tag = fullTag.substring(1, spacePos);
            else tag = fullTag.substring(1, closePos);
            tag.toLowerCase();
            bool isClosing = tag.startsWith("/");
            if(isClosing) tag = tag.substring(1);
            
            // Handle inline styling elements
            if(tag == "b" || tag == "strong" || tag == "i" || tag == "em") {
                if(!isClosing) styleStack.push_back(getStyleFromTag(tag));
                else if(styleStack.size() > 1) styleStack.pop_back();
            }
            // Handle header elements - they are BOTH styled AND block elements
            else if(tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6") {
                if(!isClosing) {
                    styleStack.push_back(getStyleFromTag(tag));
                    nextIsBlockStart = true;
                } else {
                    if(styleStack.size() > 1) styleStack.pop_back();
                    nextIsBlockStart = true;
                }
            }
            else if((tag == "p" || tag == "div" || tag.startsWith("h")) && !isClosing) {
                nextIsBlockStart = true;
                String styleAttr = extractAttribute(fullTag, tag, "style");
                String classAttr = extractAttribute(fullTag, tag, "class");
                classAttr.toLowerCase();
                
                // Detect chapter numbers/titles by CSS class
                // Be very conservative - actual headers use <h1>-<h6> tags which are handled separately
                // Only match very specific chapter/title class patterns to avoid false positives
                if(classAttr.indexOf("chapter-title") != -1 || classAttr.indexOf("chap-title") != -1 ||
                   classAttr.indexOf("section-title") != -1 || classAttr.indexOf("part-title") != -1) {
                    // This is likely a chapter/section title - use header style
                    styleStack.push_back(STYLE_HEADER1);
                }
                
                if(styleAttr.length() > 0) {
                    currentAlign = getAlignFromStyle(styleAttr);
                    currentIndent = extractIndentFromStyle(styleAttr);
                }
                if (tag == "p" && currentIndent == 0 && styleStack.back() == STYLE_NORMAL) {
                    currentIndent = 30; 
                }
            }
            else if(tag == "/p" || tag == "/div" || tag.startsWith("/h")) {
                nextIsBlockStart = true;
                // Pop any header style that was pushed for this block
                if(styleStack.size() > 1 && (styleStack.back() == STYLE_HEADER1 || 
                   styleStack.back() == STYLE_HEADER2 || styleStack.back() == STYLE_HEADER3)) {
                    styleStack.pop_back();
                }
            }
            else if(tag == "li" && !isClosing) {
                isListItem = true;
                nextIsBlockStart = true;
            }
            else if(tag == "table" && !isClosing) {
                int tableEnd = html.indexOf("</table>", i);
                if(tableEnd != -1) {
                    String tableHtml = html.substring(i, tableEnd + 8);
                    Table table = parseTable(tableHtml);
                    if(table.rows.size() > 0) {
                        ContentNode node;
                        node.type = CONTENT_TABLE;
                        node.table = table;
                        nodes.push_back(node);
                    }
                    i = tableEnd + 8;
                    nextIsBlockStart = true;
                    continue;
                }
            }
            else if(tag == "script" || tag == "style" || tag == "head" || tag == "figure" || tag == "svg" || tag == "figcaption") {
                int skipEnd = html.indexOf("</" + tag + ">", i);
                if(skipEnd != -1) { i = skipEnd + tag.length() + 3; continue; }
            }
            // Skip self-closing image tags
            else if(tag == "img" || tag == "image") {
                // Just skip - nothing to do for self-closing tags
            }
            else if(tag == "br") {
                currentText += "\n";
            }
            i = tagEnd + 1;
        } else {
            if(c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                if(currentText.length() > 0 && currentText.charAt(currentText.length()-1) != ' ' && currentText.charAt(currentText.length()-1) != '\n') {
                    currentText += ' ';
                }
            } else {
                currentText += c;
            }
            i++;
        }
    }
    if(currentText.length() > 0) {
        ContentNode node;
        node.type = CONTENT_TEXT;
        node.textNode.text = currentText;
        node.textNode.style = styleStack.back();
        node.textNode.align = currentAlign;
        node.textNode.isListItem = isListItem;
        node.textNode.indent = currentIndent;
        node.textNode.isBlockStart = nextIsBlockStart;
        nodes.push_back(node);
    }
    for(auto& node : nodes) {
        if(node.type == CONTENT_TEXT) {
            node.textNode.text.replace("¶Ç8", " -- ");
            node.textNode.text.replace("¶ÇÖ", "'");
            node.textNode.text.replace("¶Çö", "'");
            node.textNode.text.replace("¶Ç£", "\"");
            node.textNode.text.replace("¶Ç¥", "\"");
            node.textNode.text.replace("¶Ç", " ");
            node.textNode.text.replace("\xE2\x80\x9C", "\"");
            node.textNode.text.replace("\xE2\x80\x9D", "\"");
            node.textNode.text.replace("\xE2\x80\x98", "'");
            node.textNode.text.replace("\xE2\x80\x99", "'");
            node.textNode.text.replace("\xE2\x80\x94", " -- ");
            node.textNode.text.replace("\xE2\x80\x93", " - ");
            node.textNode.text.replace("\xE2\x80\xA6", "...");
            node.textNode.text.replace("\n,", ",");
            node.textNode.text.replace("\n.", ".");
            node.textNode.text.replace("\n!", "!");
            node.textNode.text.replace("\n?", "?");
            node.textNode.text.trim();
            // Filter out common image alt text placeholders
            if(node.textNode.text == "Unknown" || node.textNode.text == "image" || 
               node.textNode.text == "Image" || node.textNode.text == "[image]") {
                node.textNode.text = "";
            }
            
            // Heuristic: Short numeric content (1-3 digits) that starts a block is likely a chapter number
            if(node.textNode.isBlockStart && node.textNode.text.length() > 0 && node.textNode.text.length() <= 3) {
                bool isNumeric = true;
                for(int i = 0; i < (int)node.textNode.text.length(); i++) {
                    if(!isdigit(node.textNode.text.charAt(i))) { isNumeric = false; break; }
                }
                if(isNumeric) {
                    node.textNode.style = STYLE_HEADER1; // Chapter number - use big centered style
                }
            }
        }
    }
    // Remove empty text nodes
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const ContentNode& n) {
        return n.type == CONTENT_TEXT && n.textNode.text.length() == 0;
    }), nodes.end());
    return nodes;
}

std::vector<ContentNode> EpubLoader::getChapterContentRich(int index) {
    if(index < 0 || index >= (int)spine.size()) return std::vector<ContentNode>();
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    return readRichContentFromZip(fullPath.c_str());
}
