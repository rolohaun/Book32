#include "ReaderPageCache.h"
#include "Book32FS.h"
#include <algorithm>

namespace {

static const char* CACHE_PATH = "/reader_pages.bin";
static const uint32_t CACHE_MAGIC = 0x50323342;  // B32P
static const uint16_t CACHE_VERSION = 2;
static const uint16_t RECORD_COMPLETE = 0x0001;

struct __attribute__((packed)) CacheHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t bookHash;
    uint32_t bookSize;
    uint32_t bookFingerprint;
    uint32_t layoutKey;
};

struct __attribute__((packed)) CacheRecord {
    uint16_t chapter;
    uint16_t flags;
    uint32_t visibleOffset;
};

uint32_t hashPath(const String& value) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < value.length(); i++) {
        hash ^= static_cast<uint8_t>(value[i]);
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace

bool ReaderPageCache::begin(const String& bookPath, uint32_t bookSize, uint32_t bookFingerprint, uint32_t layoutKey) {
    clearMemory();
    CacheHeader expected = {
        CACHE_MAGIC, CACHE_VERSION, sizeof(CacheRecord), hashPath(bookPath), bookSize, bookFingerprint, layoutKey
    };

    File file = EbookFS.open(CACHE_PATH, FILE_READ);
    CacheHeader actual = {};
    bool valid = file && file.read(reinterpret_cast<uint8_t*>(&actual), sizeof(actual)) == sizeof(actual) &&
                 actual.magic == expected.magic && actual.version == expected.version &&
                 actual.recordSize == expected.recordSize && actual.bookHash == expected.bookHash &&
                 actual.bookSize == expected.bookSize && actual.bookFingerprint == expected.bookFingerprint &&
                 actual.layoutKey == expected.layoutKey;

    if (valid) {
        CacheRecord record;
        while (_recordCount < MAX_RECORDS && file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record)) {
            _recordCount++;
            if (record.flags & RECORD_COMPLETE) {
                if (!isChapterComplete(record.chapter)) _completeChapters.push_back(record.chapter);
            } else {
                bool duplicate = false;
                for (const auto& page : _pages) {
                    if (page.chapter == record.chapter && page.visibleOffset == record.visibleOffset) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) _pages.push_back({record.chapter, record.visibleOffset});
            }
        }
        file.close();
        _ready = true;
        return true;
    }
    if (file) file.close();

    EbookFS.remove(CACHE_PATH);
    file = EbookFS.open(CACHE_PATH, FILE_WRITE);
    if (!file) return false;
    bool written = file.write(reinterpret_cast<const uint8_t*>(&expected), sizeof(expected)) == sizeof(expected);
    file.close();
    _ready = written;
    return written;
}

void ReaderPageCache::clearMemory() {
    _pages.clear();
    _completeChapters.clear();
    _recordCount = 0;
    _ready = false;
}

bool ReaderPageCache::appendRecord(uint16_t chapter, uint16_t flags, uint32_t visibleOffset) {
    if (!_ready || _recordCount >= MAX_RECORDS) return false;
    File file = EbookFS.open(CACHE_PATH, FILE_APPEND);
    if (!file) return false;
    CacheRecord record = {chapter, flags, visibleOffset};
    bool written = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
    file.close();
    if (written) _recordCount++;
    return written;
}

bool ReaderPageCache::rememberPage(uint16_t chapter, uint32_t visibleOffset) {
    for (const auto& page : _pages) {
        if (page.chapter == chapter && page.visibleOffset == visibleOffset) return true;
    }
    if (!appendRecord(chapter, 0, visibleOffset)) return false;
    _pages.push_back({chapter, visibleOffset});
    return true;
}

bool ReaderPageCache::markChapterComplete(uint16_t chapter) {
    if (isChapterComplete(chapter)) return true;
    if (!appendRecord(chapter, RECORD_COMPLETE, 0)) return false;
    _completeChapters.push_back(chapter);
    return true;
}

bool ReaderPageCache::isChapterComplete(uint16_t chapter) const {
    for (uint16_t complete : _completeChapters) {
        if (complete == chapter) return true;
    }
    return false;
}

std::vector<uint32_t> ReaderPageCache::pagesForChapter(uint16_t chapter) const {
    std::vector<uint32_t> offsets;
    for (const auto& page : _pages) {
        if (page.chapter == chapter) offsets.push_back(page.visibleOffset);
    }
    std::sort(offsets.begin(), offsets.end());
    return offsets;
}
