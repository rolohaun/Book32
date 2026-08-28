#include "EpubImage.h"

#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>

namespace {

bool isJpeg(const uint8_t* data, size_t size) {
    return data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool isPng(const uint8_t* data, size_t size) {
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return data && size >= sizeof(signature) && memcmp(data, signature, sizeof(signature)) == 0;
}

bool dimensionsFromPngHeader(const uint8_t* data, size_t size, EpubImageInfo& info) {
    if (!isPng(data, size) || size < 24 || memcmp(data + 12, "IHDR", 4) != 0) return false;
    info.width = static_cast<int>((static_cast<uint32_t>(data[16]) << 24) |
                                  (static_cast<uint32_t>(data[17]) << 16) |
                                  (static_cast<uint32_t>(data[18]) << 8) | data[19]);
    info.height = static_cast<int>((static_cast<uint32_t>(data[20]) << 24) |
                                   (static_cast<uint32_t>(data[21]) << 16) |
                                   (static_cast<uint32_t>(data[22]) << 8) | data[23]);
    return info.width > 0 && info.height > 0;
}

bool dimensionsFromJpegHeader(const uint8_t* data, size_t size, EpubImageInfo& info) {
    if (!isJpeg(data, size)) return false;
    size_t position = 2;
    while (position + 8 < size) {
        while (position < size && data[position] != 0xFF) position++;
        while (position < size && data[position] == 0xFF) position++;
        if (position >= size) break;
        uint8_t marker = data[position++];
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (position + 2 > size) break;
        uint16_t length = static_cast<uint16_t>((data[position] << 8) | data[position + 1]);
        if (length < 2 || position + length > size) break;
        bool startOfFrame = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
        if (startOfFrame && length >= 7) {
            info.height = static_cast<int>((data[position + 3] << 8) | data[position + 4]);
            info.width = static_cast<int>((data[position + 5] << 8) | data[position + 6]);
            return info.width > 0 && info.height > 0;
        }
        position += length;
    }
    return false;
}

void fitDimensions(int sourceWidth, int sourceHeight, int maxWidth, int maxHeight, int& width, int& height) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || maxWidth <= 0 || maxHeight <= 0) {
        width = 0;
        height = 0;
        return;
    }

    width = sourceWidth;
    height = sourceHeight;
    if (width > maxWidth) {
        height = static_cast<int>((static_cast<int64_t>(height) * maxWidth) / width);
        width = maxWidth;
    }
    if (height > maxHeight) {
        width = static_cast<int>((static_cast<int64_t>(width) * maxHeight) / height);
        height = maxHeight;
    }
    width = max(1, width);
    height = max(1, height);
}

uint8_t* allocatePsram(size_t bytes, bool clear = false) {
    if (bytes == 0) return nullptr;
    uint8_t* result = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!result) result = static_cast<uint8_t*>(malloc(bytes));
    if (result && clear) memset(result, 0, bytes);
    return result;
}

int pngBytesPerPixel(int pixelType) {
    switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR:
            return 3;
        case PNG_PIXEL_GRAY_ALPHA:
            return 2;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            return 4;
        default:
            return 1;
    }
}

int pngPackedRowBytes(int width, int pixelType, int bitsPerPixel) {
    if ((pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) && bitsPerPixel < 8) {
        return (width * bitsPerPixel + 7) / 8;
    }
    return width * pngBytesPerPixel(pixelType);
}

int requiredPngBufferBytes(int width, int pixelType, int bitsPerPixel) {
    const int pitch = pngPackedRowBytes(width, pixelType, bitsPerPixel);
    return ((pitch + 1) * 2) + 32;
}

bool isSupportedPngFormat(int pixelType, int bitsPerPixel) {
    if (pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) {
        return bitsPerPixel == 1 || bitsPerPixel == 2 || bitsPerPixel == 4 || bitsPerPixel == 8;
    }
    return bitsPerPixel == 8;
}

struct JpegContext {
    uint8_t* gray = nullptr;
    uint8_t* intermediate = nullptr;
    int scaledSourceWidth = 0;
    int scaledSourceHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    uint32_t lastYield = 0;
};

int jpegDraw(JPEGDRAW* draw) {
    JpegContext* context = static_cast<JpegContext*>(draw->pUser);
    if (!context || !context->gray || !draw->pPixels) return 0;

    if (millis() - context->lastYield >= 8) {
        yield();
        context->lastYield = millis();
    }

    const uint8_t* pixels = reinterpret_cast<const uint8_t*>(draw->pPixels);
    const int sourceStride = draw->iWidth;
    const int validWidth = draw->iWidthUsed;
    if (context->intermediate) {
        int copyWidth = min(validWidth, context->scaledSourceWidth - draw->x);
        int copyHeight = min(draw->iHeight, context->scaledSourceHeight - draw->y);
        if (copyWidth <= 0 || copyHeight <= 0) return 1;
        for (int row = 0; row < copyHeight; row++) {
            memcpy(context->intermediate + static_cast<size_t>(draw->y + row) * context->scaledSourceWidth + draw->x,
                   pixels + static_cast<size_t>(row) * sourceStride, copyWidth);
        }
        return 1;
    }

    const int sourceEndX = draw->x + validWidth;
    const int sourceEndY = draw->y + draw->iHeight;
    int targetStartX = static_cast<int>((static_cast<int64_t>(draw->x) * context->targetWidth) /
                                        context->scaledSourceWidth);
    int targetEndX = sourceEndX >= context->scaledSourceWidth
                         ? context->targetWidth
                         : static_cast<int>((static_cast<int64_t>(sourceEndX) * context->targetWidth) /
                                            context->scaledSourceWidth);
    int targetStartY = static_cast<int>((static_cast<int64_t>(draw->y) * context->targetHeight) /
                                        context->scaledSourceHeight);
    int targetEndY = sourceEndY >= context->scaledSourceHeight
                         ? context->targetHeight
                         : static_cast<int>((static_cast<int64_t>(sourceEndY) * context->targetHeight) /
                                            context->scaledSourceHeight);

    targetStartX = constrain(targetStartX, 0, context->targetWidth);
    targetEndX = constrain(targetEndX, 0, context->targetWidth);
    targetStartY = constrain(targetStartY, 0, context->targetHeight);
    targetEndY = constrain(targetEndY, 0, context->targetHeight);

    for (int targetY = targetStartY; targetY < targetEndY; targetY++) {
        int sourceY = static_cast<int>((static_cast<int64_t>(targetY) * context->scaledSourceHeight) /
                                       context->targetHeight) - draw->y;
        sourceY = constrain(sourceY, 0, draw->iHeight - 1);
        const uint8_t* sourceRow = pixels + sourceY * sourceStride;
        uint8_t* targetRow = context->gray + targetY * context->targetWidth;
        for (int targetX = targetStartX; targetX < targetEndX; targetX++) {
            int sourceX = static_cast<int>((static_cast<int64_t>(targetX) * context->scaledSourceWidth) /
                                           context->targetWidth) - draw->x;
            sourceX = constrain(sourceX, 0, validWidth - 1);
            targetRow[targetX] = sourceRow[sourceX];
        }
    }
    return 1;
}

void resampleBilinear(const uint8_t* source, int sourceWidth, int sourceHeight,
                      uint8_t* target, int targetWidth, int targetHeight) {
    if (!source || !target || sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) return;
    if (sourceWidth == targetWidth && sourceHeight == targetHeight) {
        memcpy(target, source, static_cast<size_t>(targetWidth) * targetHeight);
        return;
    }

    for (int y = 0; y < targetHeight; y++) {
        uint32_t sourceY = targetHeight > 1
                               ? static_cast<uint32_t>((static_cast<uint64_t>(y) * (sourceHeight - 1) << 16) /
                                                       (targetHeight - 1))
                               : 0;
        int y0 = sourceY >> 16;
        int y1 = min(y0 + 1, sourceHeight - 1);
        uint32_t fy = sourceY & 0xFFFF;
        const uint8_t* row0 = source + static_cast<size_t>(y0) * sourceWidth;
        const uint8_t* row1 = source + static_cast<size_t>(y1) * sourceWidth;
        uint8_t* output = target + static_cast<size_t>(y) * targetWidth;

        for (int x = 0; x < targetWidth; x++) {
            uint32_t sourceX = targetWidth > 1
                                   ? static_cast<uint32_t>((static_cast<uint64_t>(x) * (sourceWidth - 1) << 16) /
                                                           (targetWidth - 1))
                                   : 0;
            int x0 = sourceX >> 16;
            int x1 = min(x0 + 1, sourceWidth - 1);
            uint32_t fx = sourceX & 0xFFFF;
            uint32_t top = (row0[x0] * (65536U - fx) + row0[x1] * fx) >> 16;
            uint32_t bottom = (row1[x0] * (65536U - fx) + row1[x1] * fx) >> 16;
            output[x] = static_cast<uint8_t>((top * (65536U - fy) + bottom * fy) >> 16);
        }
        if ((y & 15) == 0) yield();
    }
}

int chooseJpegScale(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight, int& denominator) {
    int64_t targetScaleX = (static_cast<int64_t>(targetWidth) * 1000) / sourceWidth;
    int64_t targetScaleY = (static_cast<int64_t>(targetHeight) * 1000) / sourceHeight;
    int64_t targetScale = min(targetScaleX, targetScaleY);
    if (targetScale <= 125) {
        denominator = 8;
        return JPEG_SCALE_EIGHTH;
    }
    if (targetScale <= 250) {
        denominator = 4;
        return JPEG_SCALE_QUARTER;
    }
    if (targetScale <= 500) {
        denominator = 2;
        return JPEG_SCALE_HALF;
    }
    denominator = 1;
    return 0;
}

struct PngContext {
    PNG* decoder = nullptr;
    uint8_t* gray = nullptr;
    uint8_t* sourceLine = nullptr;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    int lastTargetRow = -1;
    uint32_t lastYield = 0;
};

uint8_t readPackedSample(const uint8_t* pixels, int x, int bitsPerSample) {
    if (bitsPerSample == 8) return pixels[x];
    int bitOffset = x * bitsPerSample;
    int shift = 8 - bitsPerSample - (bitOffset & 7);
    uint8_t mask = static_cast<uint8_t>((1U << bitsPerSample) - 1U);
    return static_cast<uint8_t>((pixels[bitOffset >> 3] >> shift) & mask);
}

uint8_t expandSample(uint8_t sample, int bitsPerSample) {
    if (bitsPerSample == 8) return sample;
    return static_cast<uint8_t>((sample * 255U) / ((1U << bitsPerSample) - 1U));
}

void pngLineToGray(PNGDRAW* draw, uint8_t* output, int width, uint32_t transparentColor) {
    const uint8_t* pixels = draw->pPixels;
    int bits = draw->iBpp;
    switch (draw->iPixelType) {
        case PNG_PIXEL_GRAYSCALE:
            for (int x = 0; x < width; x++) {
                uint8_t sample = readPackedSample(pixels, x, bits);
                output[x] = (draw->iHasAlpha && sample == static_cast<uint8_t>(transparentColor))
                                ? 255
                                : expandSample(sample, bits);
            }
            break;
        case PNG_PIXEL_TRUECOLOR:
            for (int x = 0; x < width; x++) {
                const uint8_t* pixel = pixels + x * 3;
                uint32_t color = (static_cast<uint32_t>(pixel[0]) << 16) |
                                 (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
                output[x] = (draw->iHasAlpha && color == transparentColor)
                                ? 255
                                : static_cast<uint8_t>((pixel[0] * 77 + pixel[1] * 150 + pixel[2] * 29) >> 8);
            }
            break;
        case PNG_PIXEL_INDEXED:
            for (int x = 0; x < width; x++) {
                uint8_t index = readPackedSample(pixels, x, bits);
                uint8_t* color = draw->pPalette ? draw->pPalette + index * 3 : nullptr;
                uint8_t alpha = (draw->iHasAlpha && draw->pPalette) ? draw->pPalette[768 + index] : 255;
                output[x] = !color || alpha < 128
                                ? 255
                                : static_cast<uint8_t>((color[0] * 77 + color[1] * 150 + color[2] * 29) >> 8);
            }
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            for (int x = 0; x < width; x++) {
                uint8_t gray = pixels[x * 2];
                uint8_t alpha = pixels[x * 2 + 1];
                output[x] = static_cast<uint8_t>((gray * alpha + 255U * (255U - alpha)) / 255U);
            }
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            for (int x = 0; x < width; x++) {
                const uint8_t* pixel = pixels + x * 4;
                uint8_t gray = static_cast<uint8_t>((pixel[0] * 77 + pixel[1] * 150 + pixel[2] * 29) >> 8);
                uint8_t alpha = pixel[3];
                output[x] = static_cast<uint8_t>((gray * alpha + 255U * (255U - alpha)) / 255U);
            }
            break;
        default:
            memset(output, 255, width);
            break;
    }
}

int pngDraw(PNGDRAW* draw) {
    PngContext* context = static_cast<PngContext*>(draw->pUser);
    if (!context || !context->gray || !context->sourceLine) return 0;

    if (millis() - context->lastYield >= 8) {
        yield();
        context->lastYield = millis();
    }

    uint32_t transparent = context->decoder ? context->decoder->getTransparentColor() : 0;
    pngLineToGray(draw, context->sourceLine, context->sourceWidth, transparent);

    int targetStartY = (draw->y * context->targetHeight) / context->sourceHeight;
    int targetEndY = ((draw->y + 1) * context->targetHeight) / context->sourceHeight;
    if (targetEndY <= targetStartY) targetEndY = targetStartY + 1;
    if (targetStartY <= context->lastTargetRow) targetStartY = context->lastTargetRow + 1;
    targetEndY = min(targetEndY, context->targetHeight);

    for (int targetY = targetStartY; targetY < targetEndY; targetY++) {
        uint8_t* targetRow = context->gray + targetY * context->targetWidth;
        for (int targetX = 0; targetX < context->targetWidth; targetX++) {
            int sourceX = static_cast<int>((static_cast<int64_t>(targetX) * context->sourceWidth) /
                                           context->targetWidth);
            targetRow[targetX] = context->sourceLine[min(sourceX, context->sourceWidth - 1)];
        }
        context->lastTargetRow = targetY;
    }
    return 1;
}

bool decodeJpeg(const uint8_t* data, size_t size, uint8_t* gray, int targetWidth, int targetHeight,
                EpubImageInfo& sourceInfo) {
    std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
    if (!jpeg || jpeg->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), jpegDraw) != 1) return false;

    sourceInfo.width = jpeg->getWidth();
    sourceInfo.height = jpeg->getHeight();
    bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
    int denominator = 1;
    int scale = chooseJpegScale(sourceInfo.width, sourceInfo.height, targetWidth, targetHeight, denominator);
    if (progressive) {
        scale = JPEG_SCALE_EIGHTH;
        denominator = 8;
    }

    JpegContext context;
    context.gray = gray;
    context.scaledSourceWidth = (sourceInfo.width + denominator - 1) / denominator;
    context.scaledSourceHeight = (sourceInfo.height + denominator - 1) / denominator;
    context.targetWidth = targetWidth;
    context.targetHeight = targetHeight;
    context.lastYield = millis();

    size_t intermediateBytes = static_cast<size_t>(context.scaledSourceWidth) * context.scaledSourceHeight;
    if (progressive && intermediateBytes <= 2 * 1024 * 1024 &&
        intermediateBytes + (256 * 1024) < heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) {
        context.intermediate = allocatePsram(intermediateBytes);
        if (context.intermediate) memset(context.intermediate, 255, intermediateBytes);
    }

    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    jpeg->setUserPointer(&context);
    int result = jpeg->decode(0, 0, scale);
    jpeg->close();
    if (result == 1 && context.intermediate) {
        resampleBilinear(context.intermediate, context.scaledSourceWidth, context.scaledSourceHeight,
                         gray, targetWidth, targetHeight);
    }
    if (context.intermediate) free(context.intermediate);
    return result == 1;
}

bool decodePng(const uint8_t* data, size_t size, uint8_t* gray, int targetWidth, int targetHeight,
               EpubImageInfo& sourceInfo) {
    std::unique_ptr<PNG> png(new (std::nothrow) PNG());
    if (!png || png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), pngDraw) != PNG_SUCCESS) return false;

    sourceInfo.width = png->getWidth();
    sourceInfo.height = png->getHeight();
    if (sourceInfo.width <= 0 || sourceInfo.height <= 0 || sourceInfo.width > 8192) {
        png->close();
        return false;
    }
    const int pixelType = png->getPixelType();
    const int bitsPerPixel = png->getBpp();
    const int requiredBuffer = requiredPngBufferBytes(sourceInfo.width, pixelType, bitsPerPixel);
    if (!isSupportedPngFormat(pixelType, bitsPerPixel) ||
        requiredBuffer > PNG_MAX_BUFFERED_PIXELS) {
        Serial.printf("EpubImage: Unsupported PNG format or scanline buffer (%d bytes)\n", requiredBuffer);
        png->close();
        return false;
    }

    uint8_t* sourceLine = allocatePsram(sourceInfo.width);
    if (!sourceLine) {
        png->close();
        return false;
    }

    PngContext context;
    context.decoder = png.get();
    context.gray = gray;
    context.sourceLine = sourceLine;
    context.sourceWidth = sourceInfo.width;
    context.sourceHeight = sourceInfo.height;
    context.targetWidth = targetWidth;
    context.targetHeight = targetHeight;
    context.lastYield = millis();
    int result = png->decode(&context, 0);
    free(sourceLine);
    png->close();
    return result == PNG_SUCCESS;
}

bool ditherAtkinson(const uint8_t* gray, int width, int height, EpubBitmap& bitmap) {
    if (!bitmap.allocate(width, height)) return false;

    size_t errorBytes = static_cast<size_t>(width + 4) * sizeof(int16_t);
    int16_t* current = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    int16_t* next = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    int16_t* afterNext = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    bool useAtkinson = current && next && afterNext;

    static const uint8_t bayer4x4[16] = {0, 128, 32, 160, 192, 64, 224, 96,
                                         48, 176, 16, 144, 240, 112, 208, 80};
    for (int y = 0; y < height; y++) {
        uint8_t* output = bitmap.pixels() + y * bitmap.stride();
        const uint8_t* input = gray + y * width;
        for (int x = 0; x < width; x++) {
            int value = ((static_cast<int>(input[x]) - 128) * 108) / 100 + 128;
            value = constrain(value, 0, 255);
            bool white;
            if (useAtkinson) {
                int adjusted = constrain(value + current[x + 2], 0, 255);
                white = adjusted >= 128;
                int error = (adjusted - (white ? 255 : 0)) >> 3;
                current[x + 3] += error;
                current[x + 4] += error;
                next[x + 1] += error;
                next[x + 2] += error;
                next[x + 3] += error;
                afterNext[x + 2] += error;
            } else {
                white = value >= bayer4x4[((y & 3) << 2) | (x & 3)];
            }
            if (!white) output[x >> 3] |= static_cast<uint8_t>(0x80 >> (x & 7));
        }
        if (useAtkinson) {
            int16_t* oldCurrent = current;
            current = next;
            next = afterNext;
            afterNext = oldCurrent;
            memset(afterNext, 0, errorBytes);
        }
        if ((y & 15) == 0) yield();
    }

    if (current) free(current);
    if (next) free(next);
    if (afterNext) free(afterNext);
    return true;
}

}  // namespace

EpubBitmap::~EpubBitmap() { clear(); }

EpubBitmap::EpubBitmap(EpubBitmap&& other) noexcept {
    _pixels = other._pixels;
    _stride = other._stride;
    width = other.width;
    height = other.height;
    other._pixels = nullptr;
    other._stride = 0;
    other.width = 0;
    other.height = 0;
}

EpubBitmap& EpubBitmap::operator=(EpubBitmap&& other) noexcept {
    if (this == &other) return *this;
    clear();
    _pixels = other._pixels;
    _stride = other._stride;
    width = other.width;
    height = other.height;
    other._pixels = nullptr;
    other._stride = 0;
    other.width = 0;
    other.height = 0;
    return *this;
}

void EpubBitmap::clear() {
    if (_pixels) free(_pixels);
    _pixels = nullptr;
    _stride = 0;
    width = 0;
    height = 0;
}

bool EpubBitmap::allocate(int bitmapWidth, int bitmapHeight) {
    clear();
    if (bitmapWidth <= 0 || bitmapHeight <= 0) return false;
    width = bitmapWidth;
    height = bitmapHeight;
    _stride = (width + 7) / 8;
    _pixels = allocatePsram(static_cast<size_t>(_stride) * height, true);
    if (_pixels) return true;
    clear();
    return false;
}

void EpubBitmap::draw(Book32Display& display, int x, int y) const {
    if (valid()) display.drawBitmap(x, y, _pixels, width, height, GxEPD_BLACK);
}

bool EpubImageDecoder::dimensions(const uint8_t* data, size_t size, EpubImageInfo& info) {
    info = {};
    if (dimensionsFromPngHeader(data, size, info) || dimensionsFromJpegHeader(data, size, info)) return true;
    if (isJpeg(data, size)) {
        std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
        if (!jpeg || jpeg->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), nullptr) != 1) return false;
        info.width = jpeg->getWidth();
        info.height = jpeg->getHeight();
        jpeg->close();
    } else if (isPng(data, size)) {
        std::unique_ptr<PNG> png(new (std::nothrow) PNG());
        if (!png || png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), nullptr) != PNG_SUCCESS) return false;
        info.width = png->getWidth();
        info.height = png->getHeight();
        png->close();
    }
    return info.width > 0 && info.height > 0;
}

bool EpubImageDecoder::decode(const uint8_t* data, size_t size, int maxWidth, int maxHeight, EpubBitmap& bitmap) {
    EpubImageInfo source;
    if (!dimensions(data, size, source)) return false;

    int targetWidth = 0;
    int targetHeight = 0;
    fitDimensions(source.width, source.height, maxWidth, maxHeight, targetWidth, targetHeight);
    if (targetWidth <= 0 || targetHeight <= 0) return false;

    size_t grayBytes = static_cast<size_t>(targetWidth) * targetHeight;
    uint8_t* gray = allocatePsram(grayBytes);
    if (!gray) return false;
    memset(gray, 255, grayBytes);

    bool decoded = false;
    if (isJpeg(data, size)) decoded = decodeJpeg(data, size, gray, targetWidth, targetHeight, source);
    else if (isPng(data, size)) decoded = decodePng(data, size, gray, targetWidth, targetHeight, source);

    bool dithered = decoded && ditherAtkinson(gray, targetWidth, targetHeight, bitmap);
    free(gray);
    return dithered;
}
