#pragma once

#include <Arduino.h>
#include <vector>

class ReaderPageCache {
public:
    bool begin(const String& bookPath, uint32_t bookSize, uint32_t bookFingerprint, uint32_t layoutKey);
    void clearMemory();

    bool rememberPage(uint16_t chapter, uint32_t visibleOffset);
    bool markChapterComplete(uint16_t chapter);
    bool isChapterComplete(uint16_t chapter) const;
    std::vector<uint32_t> pagesForChapter(uint16_t chapter) const;

private:
    struct PageStart {
        uint16_t chapter;
        uint32_t visibleOffset;
    };

    static const size_t MAX_RECORDS = 2045;  // Header plus records fits in exactly 16 KiB.
    std::vector<PageStart> _pages;
    std::vector<uint16_t> _completeChapters;
    size_t _recordCount = 0;
    bool _ready = false;

    bool appendRecord(uint16_t chapter, uint16_t flags, uint32_t visibleOffset);
};
