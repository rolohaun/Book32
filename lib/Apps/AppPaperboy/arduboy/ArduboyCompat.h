#pragma once
// Arduboy2 for Inkade: enough of the Arduboy2, Arduboy2Beep and EEPROM APIs
// that small Arduboy games build unchanged apart from their includes (one
// #include "ArduboyCompat.h" in place of Arduboy2.h, Arduboy2Beep.h and
// EEPROM.h). Inkade calls the game's loop() once per frame, then shows the
// 128x64 screen 3x larger, plays the beeper and keeps the 1 KB EEPROM in a
// save file next to the game. Games live in their own namespace; see
// ArduboyGames.cpp for how one is added.
#include <Arduino.h>

namespace inkarduboy {

constexpr int16_t SCREEN_W = 128, SCREEN_H = 64;
constexpr uint8_t BLACK = 0, WHITE = 1, INVERT = 2;
constexpr uint8_t LEFT_BUTTON = 0x20, RIGHT_BUTTON = 0x40, UP_BUTTON = 0x80, DOWN_BUTTON = 0x10;
constexpr uint8_t A_BUTTON = 0x08, B_BUTTON = 0x04;
constexpr uint16_t EEPROM_SIZE = 1024;
constexpr uint16_t EEPROM_STORAGE_SPACE_START = 16;  // 0..15 belong to the system
constexpr uint16_t EEPROM_AUDIO_ON_OFF = 2;

struct Point { int16_t x, y; };
struct Rect { int16_t x, y; uint8_t width, height; };

class Arduboy2Audio {
public:
    static void begin() {}
    static void on();
    static void off();
    static void toggle() { if (enabled()) off(); else on(); }
    static void saveOnOff();
    static bool enabled();
};

class Arduboy2Base {
public:
    static uint8_t sBuffer[SCREEN_W * SCREEN_H / 8];
    static uint16_t frameCount;
    static Arduboy2Audio audio;

    static void begin();
    static void boot() {}
    static void bootLogo() {}
    static void flashlight() {}
    static void systemButtons() {}
    static void waitNoButtons() {}
    static void initRandomSeed() { randomSeed(esp_random()); }
    static void setFrameRate(uint8_t rate);
    static void setFrameDuration(uint8_t ms) { setFrameRate(ms ? 1000 / ms : 60); }
    static bool nextFrame();
    static bool nextFrameDEV() { return nextFrame(); }
    static bool everyXFrames(uint8_t frames) { return frames && frameCount % frames == 0; }
    static int cpuLoad() { return 0; }
    static void idle() {}

    static void pollButtons();
    static uint8_t buttonsState();
    static bool pressed(uint8_t buttons) { return (buttonsState() & buttons) == buttons; }
    static bool anyPressed(uint8_t buttons) { return buttonsState() & buttons; }
    static bool notPressed(uint8_t buttons) { return !(buttonsState() & buttons); }
    static bool justPressed(uint8_t button);
    static bool justReleased(uint8_t button);

    static void clear() { fillScreen(BLACK); }
    static void display() {}
    static void display(bool clearAfter) { if (clearAfter) clear(); }
    static void invert(bool) {}
    static void allPixelsOn(bool) {}
    static void flipVertical(bool) {}
    static void flipHorizontal(bool) {}
    static void setRGBled(uint8_t, uint8_t, uint8_t) {}
    static void setRGBled(uint8_t, uint8_t) {}
    static void digitalWriteRGB(uint8_t, uint8_t, uint8_t) {}
    static void digitalWriteRGB(uint8_t, uint8_t) {}
    static uint8_t* getBuffer() { return sBuffer; }

    static void drawPixel(int16_t x, int16_t y, uint8_t color = WHITE);
    static uint8_t getPixel(uint8_t x, uint8_t y);
    static void fillScreen(uint8_t color = WHITE);
    static void drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color = WHITE);
    static void drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t color = WHITE);
    static void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color = WHITE);
    static void drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE);
    static void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE);
    static void drawCircle(int16_t x, int16_t y, uint8_t r, uint8_t color = WHITE);
    static void fillCircle(int16_t x, int16_t y, uint8_t r, uint8_t color = WHITE);
    static void drawRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t r, uint8_t color = WHITE);
    static void fillRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t r, uint8_t color = WHITE);
    static void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                             uint8_t color = WHITE);
    static void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                             uint8_t color = WHITE);
    // Arduboy bitmaps: columns of 8 vertical pixels, bit 0 at the top.
    static void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t w, uint8_t h,
                           uint8_t color = WHITE);
    // Horizontal, Adafruit-style bitmaps.
    static void drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t w, uint8_t h,
                                 uint8_t color = WHITE);

    static bool collide(Point point, Rect rect) {
        return point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y && point.y < rect.y + rect.height;
    }
    static bool collide(Rect a, Rect b) {
        return !(b.x >= a.x + a.width || b.x + b.width <= a.x || b.y >= a.y + a.height || b.y + b.height <= a.y);
    }
};

// Text in the Arduboy's 6x8 cells (the classic 5x7 font).
class Arduboy2 : public Print, public Arduboy2Base {
public:
    using Print::write;
    size_t write(uint8_t c) override;
    static void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    static void setCursor(int16_t x, int16_t y) { cursorX = x; cursorY = y; }
    static int16_t getCursorX() { return cursorX; }
    static int16_t getCursorY() { return cursorY; }
    static void setTextColor(uint8_t color) { textColor = color; }
    static void setTextBackground(uint8_t color) { textBackground = color; }
    static void setTextSize(uint8_t size) { textSize = size ? size : 1; }
    static uint8_t getTextSize() { return textSize; }
    static void setTextWrap(bool wrap) { textWrap = wrap; }
    static void setTextRawMode(bool) {}

    static int16_t cursorX, cursorY;
    static uint8_t textColor, textBackground, textSize;
    static bool textWrap;
};

// Sprites in the Arduboy2 Sprites formats: width, height, then frames of
// vertical bytes (drawPlusMask: image and mask bytes interleaved).
class Sprites {
public:
    static void drawOverwrite(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
    static void drawErase(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
    static void drawSelfMasked(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
    static void drawPlusMask(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
    static void drawExternalMask(int16_t x, int16_t y, const uint8_t* bitmap, const uint8_t* mask,
                                 uint8_t frame, uint8_t maskFrame);
};
using SpritesB = Sprites;

// The one-voice beeper on speaker pin 1 (pin 2 maps to the same voice).
class BeepPin1 {
public:
    static uint8_t duration;
    static void begin() {}
    static void tone(uint16_t count);
    static void tone(uint16_t count, uint8_t frames);
    static void timer();
    static void noTone();
    // Timer count for a frequency, as on the Arduboy's 16 MHz clock.
    static constexpr uint16_t freq(float hz) { return uint16_t(1000000.0f / hz + 0.5f) - 1; }
};
using BeepPin2 = BeepPin1;

class EEPROMClass {
public:
    uint8_t read(int address) const;
    void write(int address, uint8_t value);
    void update(int address, uint8_t value) { write(address, value); }
    uint16_t length() const { return EEPROM_SIZE; }
    template <typename T> T& get(int address, T& value) const {
        uint8_t* p = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) p[i] = read(address + int(i));
        return value;
    }
    template <typename T> const T& put(int address, const T& value) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) write(address + int(i), p[i]);
        return value;
    }
    uint8_t operator[](int address) const { return read(address); }
};
extern EEPROMClass EEPROM;

}  // namespace inkarduboy

using inkarduboy::Arduboy2;
using inkarduboy::Arduboy2Base;
using inkarduboy::Arduboy2Audio;
using inkarduboy::Sprites;
using inkarduboy::SpritesB;
using inkarduboy::BeepPin1;
using inkarduboy::BeepPin2;
using inkarduboy::EEPROM;
using inkarduboy::Point;
using inkarduboy::Rect;
using inkarduboy::BLACK;
using inkarduboy::WHITE;
using inkarduboy::INVERT;
using inkarduboy::LEFT_BUTTON;
using inkarduboy::RIGHT_BUTTON;
using inkarduboy::UP_BUTTON;
using inkarduboy::DOWN_BUTTON;
using inkarduboy::A_BUTTON;
using inkarduboy::B_BUTTON;
using inkarduboy::EEPROM_STORAGE_SPACE_START;

// The Arduboy's screen size, as the macros games expect. Defined last, and
// only for game code: Adafruit GFX has members of the same names.
#ifndef INK_ARDUBOY_HOST
#define WIDTH 128
#define HEIGHT 64
#endif
