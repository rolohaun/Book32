/* Modified for InkDeck, 2026-10-08; see THIRD_PARTY_NOTICES.md and tools/import_nofrendo.py. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
static inline uint16_t inkNesRead16(const void* p) { uint16_t v;memcpy(&v,p,2);return v; }
static inline uint32_t inkNesRead32(const void* p) { uint32_t v;memcpy(&v,p,4);return v; }
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_attr.h>
static inline void* inkNesAlloc(size_t bytes) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#define malloc inkNesAlloc
#endif
#if !defined(ESP_PLATFORM)
// Host regression tests compare staged scanline rendering with the original.
extern bool inkNesStageRows;
#endif
static inline uint32_t inkNesCrc(uint32_t seed, const void* ptr, size_t bytes) {
    const uint8_t* data = (const uint8_t*)ptr;
    uint32_t crc = ~seed;
    while (bytes--) {
        crc ^= *data++;
        for (unsigned i=0; i<8; ++i) crc = (crc>>1) ^ (0xedb88320U & (0U-(crc&1)));
    }
    return ~crc;
}
