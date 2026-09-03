#include "StickyDisplay.h"

#if defined(BOARD_SEEED_STICKY)

#include "Config.h"
#include <cstring>

StickyDisplay::StickyDisplay() : GFXcanvas1(800, 480), _panel(EP397_800x480) {}

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
    _panel.setBuffer(getBuffer());
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    // The Sticky's controller compares two internal image planes during a
    // partial refresh. With only the new plane populated, black pixels appear
    // but old black pixels are not driven back to white. PLANE_FALSE_DIFF is
    // the bb_epaper library's Sticky-specific one-buffer path: it writes the
    // new frame and its inverse so every changed pixel is actively driven.
    // Full refreshes duplicate the frame into both planes, leaving the
    // controller in a synchronized state for the next update.
    _panel.writePlane(_partialWindow ? PLANE_FALSE_DIFF : PLANE_DUPLICATE);
    _panel.refresh(_partialWindow ? REFRESH_PARTIAL : REFRESH_FULL, false);
    SPI.endTransaction();
    _panel.wait();
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
