#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Adafruit_GFX.h>
#include <FastEPD.h>

// Native 960x540 canvas, rotated to 540x960 by the shared UI.
class LilygoDisplay : public GFXcanvas1 {
public:
    LilygoDisplay() : GFXcanvas1(960, 540) {}
    void init(uint32_t = 115200, bool = true, uint16_t = 10, bool = false);
    void setFullWindow() { _partial = false; }
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) { _partial = true; }
    void firstPage() {}
    bool nextPage() { refresh(_partial); return false; }
    void refresh(bool partial = false);
    void clearScreen(uint8_t value = 0xff);
    void hibernate();
    void setGameMode(bool active);
private:
    FASTEPD _panel;
    bool _ready = false, _partial = false, _gaming = false;
};
#endif
