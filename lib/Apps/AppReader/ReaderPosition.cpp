#include "ReaderPosition.h"

namespace {

uint32_t countCodepoints(const String& text, int byteLimit = -1) {
    int limit = byteLimit < 0 ? static_cast<int>(text.length()) : min(byteLimit, static_cast<int>(text.length()));
    uint32_t count = 0;
    for (int i = 0; i < limit; i++) {
        if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) count++;
    }
    return count;
}

int byteOffsetForCodepoints(const String& text, uint32_t codepoints) {
    if (codepoints == 0) return 0;
    uint32_t seen = 0;
    for (int i = 0; i < static_cast<int>(text.length()); i++) {
        if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) {
            if (seen == codepoints) return i;
            seen++;
        }
    }
    return text.length();
}

}  // namespace

namespace ReaderPosition {

uint32_t toVisibleOffset(const std::vector<ContentNode>& content, const PagePointer& pointer) {
    uint32_t offset = 0;
    bool sawContent = false;
    for (int i = 0; i < static_cast<int>(content.size()); i++) {
        const ContentNode& node = content[i];
        if (node.type == CONTENT_IMAGE) {
            if (i == pointer.nodeIndex) return offset;
            offset++;
            sawContent = true;
            continue;
        }
        if (node.type != CONTENT_TEXT) continue;

        if (node.textNode.isBlockStart && sawContent) offset++;
        if (i == pointer.nodeIndex) {
            return offset + countCodepoints(node.textNode.text, max(0, pointer.charOffset));
        }

        offset += countCodepoints(node.textNode.text);
        if (node.textNode.text.length() > 0) sawContent = true;
    }
    return offset;
}

PagePointer fromVisibleOffset(const std::vector<ContentNode>& content, uint32_t visibleOffset) {
    uint32_t cursor = 0;
    bool sawContent = false;
    PagePointer previousEnd = {0, 0};
    for (int i = 0; i < static_cast<int>(content.size()); i++) {
        const ContentNode& node = content[i];
        if (node.type == CONTENT_IMAGE) {
            if (visibleOffset <= cursor) return {i, 0};
            cursor++;
            previousEnd = {i + 1, 0};
            sawContent = true;
            if (visibleOffset <= cursor) return previousEnd;
            continue;
        }
        if (node.type != CONTENT_TEXT) continue;

        if (node.textNode.isBlockStart && sawContent) {
            if (visibleOffset <= cursor) return previousEnd;
            cursor++;
        }
        if (visibleOffset <= cursor) return {i, 0};

        uint32_t length = countCodepoints(node.textNode.text);
        if (visibleOffset <= cursor + length) {
            return {i, byteOffsetForCodepoints(node.textNode.text, visibleOffset - cursor)};
        }
        cursor += length;
        if (length > 0) sawContent = true;
        previousEnd = {i, static_cast<int>(node.textNode.text.length())};
    }
    return {static_cast<int>(content.size()), 0};
}

uint32_t totalVisibleLength(const std::vector<ContentNode>& content) {
    return toVisibleOffset(content, {static_cast<int>(content.size()), 0});
}

}  // namespace ReaderPosition
