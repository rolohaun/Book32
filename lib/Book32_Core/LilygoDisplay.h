#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Adafruit_GFX.h>
#include <FastEPD.h>
#include "LilygoLayout.h"
#include "NesHistoryMemory.h"

// Safe 944x524 canvas inside the physical 960x540 panel, rotated by the UI.
class LilygoDisplay : public GFXcanvas1 {
public:
    LilygoDisplay() : GFXcanvas1(LilygoLayout::CANVAS_W, LilygoLayout::CANVAS_H) {}
    void init(uint32_t = 115200, bool = true, uint16_t = 10, bool = false);
    void setFullWindow() { _partial = false; }
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) { _partial = true; }
    void firstPage() {}
    bool nextPage() { refresh(_partial); return false; }
    void refresh(bool partial = false);
    void clearScreen(uint8_t value = 0xff);
    void hibernate();
    void setFrontlight(bool on);
    void setBrightness(uint8_t percent);
    bool frontlightOn() const { return _frontlightOn; }
    void setGameMode(bool active);
    void setNesGame(bool nes) { if(_nesGame!=nes) { setGameMode(false); _nesGame=nes; } }
    // Borrowed, immutable compact frame; caller keeps it alive until refresh
    // returns and releases it only after setGameMode(false).
    void setGameFrame(const uint8_t* packed) { _gameImage = packed; }
    bool refreshGame(bool clean, bool smooth);
private:
    FASTEPD _panel;
    bool _ready = false, _partial = false, _gaming = false;
    bool _frontlightOn = false;
    uint8_t _brightness = 100;
    uint8_t* _videoHistory = nullptr;
    uint8_t* _nesHistoryBanks[NesHistoryMemory::BANKS] = {};
    const uint8_t* _gameImage = nullptr;
    bool _videoRunning = false, _videoReset = true;
    bool _videoAvailable = false;
    bool _nesGame = false;
    void stopVideo();
    void checkResult(int result);
    void copyRegion(const BB_RECT& rect);
};
#endif
