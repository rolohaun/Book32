// The Arduboy2 runtime behind ArduboyCompat.h, and Inkade's console API for
// the built-in games. Drawing goes through Adafruit GFX into the Arduboy's own
// screen layout (eight-pixel vertical bytes), so games that poke sBuffer
// directly see what they expect.
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY) || defined(BOARD_WAVESHARE_RLCD42)
#define INK_ARDUBOY_HOST
#include "ArduboyCompat.h"
#include "ArduboyCore.h"
#include "ArduboyGames.h"
#include <Adafruit_GFX.h>

namespace inkarduboy {
namespace {
struct Screen : Adafruit_GFX {
    Screen() : Adafruit_GFX(SCREEN_W, SCREEN_H) { cp437(true); }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
        uint8_t& b = Arduboy2Base::sBuffer[(y >> 3) * SCREEN_W + x];
        const uint8_t bit = 1 << (y & 7);
        if (color == WHITE) b |= bit;
        else if (color == INVERT) b ^= bit;
        else b &= ~bit;
    }
};
Screen screen;

const ArduboyGame* game = nullptr;
uint8_t eeprom[EEPROM_SIZE];
// Live buttons (pressed()), and the pair pollButtons() keeps for justPressed().
uint8_t live = 0, buttons = 0, previousButtons = 0;
bool frameDue = false, needsSetup = false;
uint8_t frameRate = 60;
uint16_t toneCount = 0;  // Arduboy timer count, 0 when silent
uint32_t phase = 0;

void plot(int16_t x, int16_t y, bool set, uint8_t color) {
    if (set) screen.drawPixel(x, y, color);
}
// Calls `pixel(px, py, bit)` for each pixel of one sprite frame.
template <typename F> void eachPixel(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame,
                                     unsigned stride, F pixel) {
    const uint8_t w = pgm_read_byte(bitmap), h = pgm_read_byte(bitmap + 1);
    const unsigned pages = (h + 7) / 8;
    const uint8_t* data = bitmap + 2 + unsigned(frame) * w * pages * stride;
    for (unsigned p = 0; p < pages; ++p)
        for (unsigned i = 0; i < w; ++i)
            for (unsigned b = 0; b < 8 && p * 8 + b < h; ++b)
                pixel(x + int16_t(i), y + int16_t(p * 8 + b), data + (p * w + i) * stride, b);
}
}  // namespace

uint8_t Arduboy2Base::sBuffer[SCREEN_W * SCREEN_H / 8];
uint16_t Arduboy2Base::frameCount = 0;
Arduboy2Audio Arduboy2Base::audio;
int16_t Arduboy2::cursorX = 0, Arduboy2::cursorY = 0;
uint8_t Arduboy2::textColor = WHITE, Arduboy2::textBackground = BLACK, Arduboy2::textSize = 1;
bool Arduboy2::textWrap = false;
uint8_t BeepPin1::duration = 0;
EEPROMClass EEPROM;

// Audio: EEPROM byte 2, as on the Arduboy (erased EEPROM reads as on).
void Arduboy2Audio::on() { eeprom[EEPROM_AUDIO_ON_OFF] = 0x80; }
void Arduboy2Audio::off() { eeprom[EEPROM_AUDIO_ON_OFF] = 0x00; BeepPin1::noTone(); }
void Arduboy2Audio::saveOnOff() {}
bool Arduboy2Audio::enabled() { return eeprom[EEPROM_AUDIO_ON_OFF] != 0; }

void Arduboy2Base::begin() { clear(); }
void Arduboy2Base::setFrameRate(uint8_t rate) { frameRate = rate ? rate : 60; }
bool Arduboy2Base::nextFrame() {
    if (!frameDue) return false;
    frameDue = false;
    ++frameCount;
    return true;
}
void Arduboy2Base::pollButtons() { previousButtons = buttons; buttons = live; }
uint8_t Arduboy2Base::buttonsState() { return live; }
bool Arduboy2Base::justPressed(uint8_t b) { return (buttons & b) && !(previousButtons & b); }
bool Arduboy2Base::justReleased(uint8_t b) { return !(buttons & b) && (previousButtons & b); }

void Arduboy2Base::drawPixel(int16_t x, int16_t y, uint8_t c) { screen.drawPixel(x, y, c); }
uint8_t Arduboy2Base::getPixel(uint8_t x, uint8_t y) {
    if (x >= SCREEN_W || y >= SCREEN_H) return BLACK;
    return (sBuffer[(y >> 3) * SCREEN_W + x] >> (y & 7)) & 1;
}
void Arduboy2Base::fillScreen(uint8_t c) { memset(sBuffer, c == WHITE ? 0xFF : 0x00, sizeof(sBuffer)); }
void Arduboy2Base::drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t c) { screen.drawFastHLine(x, y, w, c); }
void Arduboy2Base::drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t c) { screen.drawFastVLine(x, y, h, c); }
void Arduboy2Base::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c) { screen.drawLine(x0, y0, x1, y1, c); }
void Arduboy2Base::drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c) { screen.drawRect(x, y, w, h, c); }
void Arduboy2Base::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c) { screen.fillRect(x, y, w, h, c); }
void Arduboy2Base::drawCircle(int16_t x, int16_t y, uint8_t r, uint8_t c) { screen.drawCircle(x, y, r, c); }
void Arduboy2Base::fillCircle(int16_t x, int16_t y, uint8_t r, uint8_t c) { screen.fillCircle(x, y, r, c); }
void Arduboy2Base::drawRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t r, uint8_t c) {
    screen.drawRoundRect(x, y, w, h, r, c);
}
void Arduboy2Base::fillRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t r, uint8_t c) {
    screen.fillRoundRect(x, y, w, h, r, c);
}
void Arduboy2Base::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c) {
    screen.drawTriangle(x0, y0, x1, y1, x2, y2, c);
}
void Arduboy2Base::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c) {
    screen.fillTriangle(x0, y0, x1, y1, x2, y2, c);
}
void Arduboy2Base::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t w, uint8_t h, uint8_t c) {
    for (unsigned p = 0; p < unsigned(h + 7) / 8; ++p)
        for (unsigned i = 0; i < w; ++i) {
            const uint8_t bits = pgm_read_byte(bitmap + p * w + i);
            for (unsigned b = 0; b < 8 && p * 8 + b < h; ++b) plot(x + i, y + p * 8 + b, bits & (1 << b), c);
        }
}
void Arduboy2Base::drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t w, uint8_t h, uint8_t c) {
    screen.drawBitmap(x, y, bitmap, w, h, c);
}

size_t Arduboy2::write(uint8_t c) {
    if (c == '\n') { cursorY += 8 * textSize; cursorX = 0; return 1; }
    if (c == '\r') return 1;
    if (textWrap && cursorX + 6 * textSize > SCREEN_W) { cursorY += 8 * textSize; cursorX = 0; }
    drawChar(cursorX, cursorY, c, textColor, textBackground, textSize);
    cursorX += 6 * textSize;
    return 1;
}
void Arduboy2::drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size) {
    // The Arduboy fills the sixth column and the eighth row with background.
    screen.drawChar(x, y, c, color, bg == color ? color : bg, size);
    if (bg != color) {
        screen.fillRect(x + 5 * size, y, size, 8 * size, bg);
        screen.fillRect(x, y + 7 * size, 6 * size, size, bg);
    }
}

void Sprites::drawOverwrite(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame) {
    eachPixel(x, y, bitmap, frame, 1, [](int16_t px, int16_t py, const uint8_t* d, unsigned b) {
        screen.drawPixel(px, py, (pgm_read_byte(d) >> b) & 1 ? WHITE : BLACK);
    });
}
void Sprites::drawErase(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame) {
    eachPixel(x, y, bitmap, frame, 1, [](int16_t px, int16_t py, const uint8_t* d, unsigned b) {
        plot(px, py, (pgm_read_byte(d) >> b) & 1, BLACK);
    });
}
void Sprites::drawSelfMasked(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame) {
    eachPixel(x, y, bitmap, frame, 1, [](int16_t px, int16_t py, const uint8_t* d, unsigned b) {
        plot(px, py, (pgm_read_byte(d) >> b) & 1, WHITE);
    });
}
void Sprites::drawPlusMask(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame) {
    eachPixel(x, y, bitmap, frame, 2, [](int16_t px, int16_t py, const uint8_t* d, unsigned b) {
        if ((pgm_read_byte(d + 1) >> b) & 1) screen.drawPixel(px, py, (pgm_read_byte(d) >> b) & 1 ? WHITE : BLACK);
    });
}
void Sprites::drawExternalMask(int16_t x, int16_t y, const uint8_t* bitmap, const uint8_t* mask,
                               uint8_t frame, uint8_t maskFrame) {
    const uint8_t w = pgm_read_byte(bitmap), h = pgm_read_byte(bitmap + 1);
    const unsigned pages = (h + 7) / 8, frameBytes = unsigned(w) * pages;
    const uint8_t* image = bitmap + 2 + frame * frameBytes;
    const uint8_t* cover = mask + maskFrame * frameBytes;
    for (unsigned p = 0; p < pages; ++p)
        for (unsigned i = 0; i < w; ++i) {
            const uint8_t bits = pgm_read_byte(image + p * w + i), m = pgm_read_byte(cover + p * w + i);
            for (unsigned b = 0; b < 8 && p * 8 + b < h; ++b)
                if ((m >> b) & 1) screen.drawPixel(x + i, y + p * 8 + b, (bits >> b) & 1 ? WHITE : BLACK);
        }
}

void BeepPin1::tone(uint16_t count) { toneCount = count; duration = 0; }
void BeepPin1::tone(uint16_t count, uint8_t frames) { toneCount = count; duration = frames; }
void BeepPin1::timer() { if (duration && --duration == 0) toneCount = 0; }
void BeepPin1::noTone() { toneCount = 0; duration = 0; }

uint8_t EEPROMClass::read(int address) const {
    return address >= 0 && address < EEPROM_SIZE ? eeprom[address] : 0xFF;
}
void EEPROMClass::write(int address, uint8_t value) {
    if (address >= 0 && address < EEPROM_SIZE) eeprom[address] = value;
}
}  // namespace inkarduboy

using namespace inkarduboy;

namespace {
String paths[ARDUBOY_GAME_COUNT];
}

unsigned inkArduboyCount(void) { return ARDUBOY_GAME_COUNT; }

const char* inkArduboyPath(unsigned index) {
    if (index >= ARDUBOY_GAME_COUNT) return "";
    if (paths[index].isEmpty()) {
        String title = ARDUBOY_GAMES[index].title;
        title.replace(' ', '_');
        paths[index] = "/roms/" + title + ".arduboy";
    }
    return paths[index].c_str();
}

int inkArduboyFind(const char* path) {
    for (unsigned i = 0; i < ARDUBOY_GAME_COUNT; ++i)
        if (!strcmp(path, inkArduboyPath(i))) return int(i);
    return -1;
}

bool inkArduboyOpen(unsigned index) {
    if (index >= ARDUBOY_GAME_COUNT) return false;
    game = &ARDUBOY_GAMES[index];
    needsSetup = true;
    memset(eeprom, 0xFF, sizeof(eeprom));  // erased, until Inkade loads the save
    live = buttons = previousButtons = 0;
    frameDue = false;
    frameRate = 60;
    toneCount = 0;
    Arduboy2::cursorX = Arduboy2::cursorY = 0;
    Arduboy2::textColor = WHITE; Arduboy2::textBackground = BLACK; Arduboy2::textSize = 1;
    Arduboy2::textWrap = false;
    Arduboy2Base::clear();
    return true;
}

void inkArduboyClose(void) { game = nullptr; toneCount = 0; }

// Games read the save in setup(), so it runs on the first frame, after
// Inkade has filled the EEPROM from the save file.
bool inkArduboyFrame(uint16_t pad, uint8_t* shades) {
    if (!game) return false;
    if (needsSetup) { needsSetup = false; game->setup(); }
    // Inkade bits: A 1, B 2, Select 4, Start 8, Right 16, Left 32, Up 64, Down 128.
    live = (pad & 1 ? B_BUTTON : 0) | (pad & 2 ? A_BUTTON : 0) | (pad & 0x10 ? RIGHT_BUTTON : 0) |
              (pad & 0x20 ? LEFT_BUTTON : 0) | (pad & 0x40 ? UP_BUTTON : 0) | (pad & 0x80 ? DOWN_BUTTON : 0);
    frameDue = true;
    game->loop();
    frameDue = false;
    return inkArduboyImage(shades);
}

bool inkArduboyImage(uint8_t* shades) {
    if (!shades) return false;
#if defined(BOARD_WAVESHARE_RLCD42)
    // The RLCD scales 128x64 itself (3x): the screen alone, 128 wide.
    constexpr int PITCH = SCREEN_W, LEFT = 0, TOP = 0;
#else
    // Elsewhere it sits in the middle of the Game Boy frame (160x144), which
    // the board's game window then scales whole.
    constexpr int PITCH = 160, LEFT = (160 - SCREEN_W) / 2, TOP = (144 - SCREEN_H) / 2;
    memset(shades, 0, 160 * 144);
#endif
    // Lit Arduboy pixels are ink on the paper's white.
    for (int y = 0; y < SCREEN_H; ++y)
        for (int x = 0; x < SCREEN_W; ++x)
            shades[(TOP + y) * PITCH + LEFT + x] =
                (Arduboy2Base::sBuffer[(y >> 3) * SCREEN_W + x] >> (y & 7)) & 1 ? 3 : 0;
    return true;
}

unsigned inkArduboyToneHz(void) {
    return game && toneCount && Arduboy2Audio::enabled() ? 1000000U / (toneCount + 1U) : 0;
}

uint8_t* inkArduboyRam(size_t* bytes) {
    *bytes = game ? sizeof(eeprom) : 0;
    return game ? eeprom : nullptr;
}

unsigned inkArduboyPeriod(void) { return 1000000U / frameRate; }

// The beeper as a square wave: count + 1 microseconds per cycle, as
// BeepPin1::freq() computes it.
size_t inkArduboyAudio(int16_t* out, size_t max) {
    if (!out) return 0;
    size_t samples = 44100U * inkArduboyPeriod() / 1000000U;
    if (samples > max) samples = max;
    if (!toneCount || !Arduboy2Audio::enabled()) {
        memset(out, 0, samples * sizeof(int16_t));
        phase = 0;
        return samples;
    }
    const uint32_t step = uint32_t((1000000ULL << 32) / (uint64_t(toneCount + 1) * 44100U) >> 16);
    for (size_t i = 0; i < samples; ++i) {
        phase += step;
        out[i] = (phase & 0x8000) ? 5000 : -5000;
    }
    return samples;
}

const char* inkArduboyName(void) { return game ? game->title : "Arduboy"; }
#endif
