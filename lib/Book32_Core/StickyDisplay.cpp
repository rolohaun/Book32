#include "StickyDisplay.h"

#if defined(BOARD_SEEED_STICKY)

#include "Config.h"
#include <cstring>
#include <esp_heap_caps.h>

namespace {
constexpr size_t FRAME_BYTES = ((800 + 7) / 8) * 480;
}

StickyDisplay::StickyDisplay() : GFXcanvas1(800, 480), _panel(EP397_800x480) {
    _previousBuffer = static_cast<uint8_t*>(
        heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_previousBuffer) _previousBuffer = static_cast<uint8_t*>(malloc(FRAME_BYTES));
    if (_previousBuffer) memset(_previousBuffer, 0xFF, FRAME_BYTES);
}

void StickyDisplay::init(uint32_t, bool, uint16_t, bool) {
    pinMode(EPD_ENABLE, OUTPUT);
    digitalWrite(EPD_ENABLE, HIGH);
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    _panel.initIO(EPD_DC, EPD_RST, EPD_BUSY, EPD_CS, EPD_MOSI, EPD_SCK, 8000000);
    _panel.setBuffer(getBuffer());
    clearScreen();
}

void StickyDisplay::setFullWindow() {
    _partialWindow = false;
}

void StickyDisplay::setPartialWindow(int16_t, int16_t, int16_t, int16_t) {
    // InkDeck draws a complete composited frame even for a partial update. Keep
    // the full buffer valid, but select the panel's low-flash partial waveform.
    _partialWindow = true;
}

void StickyDisplay::firstPage() {
    _pagePending = true;
}

bool StickyDisplay::nextPage() {
    if (_pagePending) {
        flush();
        _pagePending = false;
    }
    return false;
}

void StickyDisplay::flush() {
    if (!getBuffer()) return;
    digitalWrite(SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    if (_partialWindow && _previousBuffer) {
        // SSD1677 partial refresh compares an old plane with a new plane.
        // Supplying both avoids the forced all-pixel update (and its flash)
        // while still driving erased black pixels cleanly back to white.
        _panel.setBuffer(_previousBuffer);
        _panel.writePlane(PLANE_1);
        _panel.setBuffer(getBuffer());
        _panel.writePlane(PLANE_0);
    } else {
        _panel.setBuffer(getBuffer());
        _panel.writePlane(_partialWindow ? PLANE_FALSE_DIFF : PLANE_DUPLICATE);
    }
    _panel.refresh(_partialWindow ? REFRESH_PARTIAL : REFRESH_FULL, false);
    SPI.endTransaction();
    _panel.wait();
    if (_previousBuffer) memcpy(_previousBuffer, getBuffer(), FRAME_BYTES);
}

void StickyDisplay::refresh(bool partialUpdateMode) {
    _partialWindow = partialUpdateMode;
    flush();
}

void StickyDisplay::clearScreen(uint8_t value) {
    if (!getBuffer()) return;
    memset(getBuffer(), value, ((800 + 7) / 8) * 480);
    _partialWindow = false;
    flush();
}

#endif
