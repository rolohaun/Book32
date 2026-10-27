#pragma once
#include "Config.h"
#if defined(BOARD_WAVESHARE_RLCD42)
#include <Adafruit_GFX.h>
#include <SPI.h>

// Waveshare ESP32-S3-RLCD-4.2: a 400x300 one-bit ST7305 reflective LCD.
// The canvas is the panel in landscape (rotation 0, as Waveshare's demos
// show it); the UI turns it to 300x400 portrait with rotation 1 or 3, and
// Inkade games draw in landscape. Every refresh sends the whole 15 KB frame,
// which takes a few milliseconds, so there is no partial update to manage.
//
// GxEPD2 semantics are kept for the Book32 code paths that this board
// shares: inside a partial window, fillScreen() clears only that window and
// drawing outside it is ignored.
class RlcdDisplay : public GFXcanvas1 {
public:
    static constexpr int NATIVE_W = 400, NATIVE_H = 300;

    RlcdDisplay();
    void init(uint32_t serialDiagBaud = 115200, bool initial = true,
              uint16_t resetDuration = 10, bool pulldownReset = false);
    void setFullWindow() { _windowed = false; }
    void setPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h);
    void firstPage() {}
    bool nextPage() { refresh(); return false; }
    void refresh(bool partialUpdateMode = false);
    void clearScreen(uint8_t value = 0xFF);
    // Low power mode: the controller keeps showing its picture from its own
    // memory while the ESP32-S3 deep sleeps.
    void hibernate();
    void powerOff() {}

    // GFXcanvas1, clipped to the partial window.
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;

    // The panel has no gray levels; covers fall back to their dithered form.
    bool drawGrayCover(uint8_t*, int, int, int, int) { return false; }

    // Inkade video. The frame holds one shade (0 white .. 3 black) per
    // source pixel: Game Boy 160x144 or NES 256x240 (GameViewport and
    // NesViewport). It is scaled into the landscape game window and dithered;
    // the rest of the canvas is left as painted.
    void setGameMode(bool active) { _gaming = active; }
    void setNesGame(bool nes) { _nesGame = nes; }
    // Arduboy games: 128x64 (the start of the frame), shown 3x as large.
    void setArduboyGame(bool arduboy) { _arduboyGame = arduboy; }
    static constexpr int ARDUBOY_X = 8, ARDUBOY_Y = 54, ARDUBOY_W = 384, ARDUBOY_H = 192;
    // Borrowed until the next setGameFrame or setGameMode(false).
    void setGameFrame(const uint8_t* frame) { _gameFrame = frame; }
    // 0 or 2: the landscape rotation the game is played in.
    void setGameRotation(uint8_t rotation) { _gameRotation = rotation == 2 ? 2 : 0; }
    bool refreshGame(bool clean = false, bool smooth = true);

private:
    SPIClass _spi;
    uint8_t* _panel = nullptr;  // The frame in controller order
    bool _ready = false, _windowed = false, _gaming = false, _nesGame = false, _arduboyGame = false;
    int16_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0;  // Partial window, rotated coordinates
    const uint8_t* _gameFrame = nullptr;
    uint8_t _gameRotation = 0;

    bool inWindow(int16_t x, int16_t y) const {
        return !_windowed || (x >= _wx0 && x < _wx1 && y >= _wy0 && y < _wy1);
    }
    void command(uint8_t cmd, const uint8_t* data = nullptr, size_t length = 0);
    void reset();
    void drawGameFrame();
    void drawArduboyFrame();
    void flush();
};
#endif
