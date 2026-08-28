#pragma once

#if defined(BOARD_SEEED_STICKY)

#include <Adafruit_GFX.h>
#include <bb_epaper.h>
#include <SPI.h>

// GFX-compatible adapter for the Sticky's native-landscape SSD1677 panel.
// Book32 keeps one 800x480 framebuffer, so existing GFX font and bitmap code
// can run unchanged while bb_epaper handles the panel-specific waveform.
class StickyDisplay : public GFXcanvas1 {
public:
    StickyDisplay();

    void init(uint32_t serialDiagBaud = 115200, bool initial = true,
              uint16_t resetDuration = 10, bool pulldownReset = false);
    void setFullWindow();
    void setPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h);
    void firstPage();
    bool nextPage();
    void refresh(bool partialUpdateMode = false);
    void clearScreen(uint8_t value = 0xFF);

private:
    BBEPAPER _panel;
    bool _partialWindow = false;
    bool _pagePending = false;

    void flush();
};

#endif
