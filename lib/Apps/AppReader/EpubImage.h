#pragma once

#include <Arduino.h>
#include "DisplayMgr.h"

struct EpubImageInfo {
    int width = 0;
    int height = 0;
};

class EpubBitmap {
public:
    EpubBitmap() = default;
    ~EpubBitmap();
    EpubBitmap(const EpubBitmap&) = delete;
    EpubBitmap& operator=(const EpubBitmap&) = delete;
    EpubBitmap(EpubBitmap&& other) noexcept;
    EpubBitmap& operator=(EpubBitmap&& other) noexcept;

    bool valid() const { return _pixels != nullptr && width > 0 && height > 0; }
    bool allocate(int bitmapWidth, int bitmapHeight);
    uint8_t* pixels() { return _pixels; }
    int stride() const { return _stride; }
    void clear();
    void draw(Book32Display& display, int x, int y) const;

    int width = 0;
    int height = 0;

private:
    uint8_t* _pixels = nullptr;
    int _stride = 0;
};

class EpubImageDecoder {
public:
    static bool dimensions(const uint8_t* data, size_t size, EpubImageInfo& info);
    static bool decode(const uint8_t* data, size_t size, int maxWidth, int maxHeight, EpubBitmap& bitmap);
};
