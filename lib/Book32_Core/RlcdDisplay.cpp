#include "RlcdDisplay.h"
#if defined(BOARD_WAVESHARE_RLCD42)
#include <esp_heap_caps.h>

namespace {
// Waveshare's U8g2 example drives the ST7305 at 24 MHz.
constexpr uint32_t SPI_HZ = 24000000;
constexpr int PANEL_BYTES = RlcdDisplay::NATIVE_W * RlcdDisplay::NATIVE_H / 8;
constexpr int CANVAS_PITCH = RlcdDisplay::NATIVE_W / 8;

// Landscape game window: Game Boy at 2x (320x288), NES at 1.25x (320x300).
constexpr int GAME_X = 40, GAME_W = 320;
constexpr int GB_Y = 6, GB_H = 288, NES_Y = 0, NES_H = 300;
static_assert((GAME_X & 7) == 0 && (GAME_W & 7) == 0, "byte-aligned game window");
static_assert(GAME_X * 2 + GAME_W == RlcdDisplay::NATIVE_W, "centered, so 180 degrees keeps it aligned");
static_assert(GB_Y * 2 + GB_H == RlcdDisplay::NATIVE_H && NES_Y * 2 + NES_H == RlcdDisplay::NATIVE_H,
              "centered vertically");

// Four shades on a one-bit panel: 0, 1, 3 or 4 black pixels of each 2x2
// cell (the Sticky's Game Boy coverage, here by panel position).
constexpr uint8_t THRESHOLD[2][2] = {{0, 2}, {3, 1}};
constexpr uint8_t DENSITY[4] = {0, 1, 3, 4};

// ST7305 power-up, from Waveshare's examples for this board.
struct InitStep { uint8_t command, length; uint8_t data[10]; };
constexpr InitStep INIT[] = {
    {0xD6, 2, {0x17, 0x02}},        // NVM load control
    {0xD1, 1, {0x01}},              // Booster enable
    {0xC0, 2, {0x11, 0x04}},        // Gate voltage
    {0xC1, 4, {0x69, 0x69, 0x69, 0x69}},  // Source voltages
    {0xC2, 4, {0x19, 0x19, 0x19, 0x19}},
    {0xC4, 4, {0x4B, 0x4B, 0x4B, 0x4B}},
    {0xC5, 4, {0x19, 0x19, 0x19, 0x19}},
    {0xD8, 2, {0x80, 0xE9}},
    {0xB2, 1, {0x02}},              // Frame rate
    {0xB3, 10, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}},
    {0xB4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}},
    {0x62, 3, {0x32, 0x03, 0x1F}},  // Gate timing
    {0xB7, 1, {0x13}},
    {0xB0, 1, {0x64}},
};
constexpr InitStep AFTER_WAKE[] = {
    {0xC9, 1, {0x00}},
    {0x36, 1, {0x48}},              // Memory access order
    {0x3A, 1, {0x11}},              // One bit per pixel
    {0xB9, 1, {0x20}},
    {0xB8, 1, {0x29}},
};
constexpr uint8_t COLUMNS[] = {0x12, 0x2A}, ROWS[] = {0x00, 0xC7};
}

RlcdDisplay::RlcdDisplay() : GFXcanvas1(NATIVE_W, NATIVE_H), _spi(FSPI) {}

void RlcdDisplay::command(uint8_t cmd, const uint8_t* data, size_t length) {
    _spi.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(RLCD_CS, LOW);
    digitalWrite(RLCD_DC, LOW);
    _spi.transfer(cmd);
    if (length) {
        digitalWrite(RLCD_DC, HIGH);
        _spi.writeBytes(data, length);
    }
    digitalWrite(RLCD_CS, HIGH);
    _spi.endTransaction();
}

void RlcdDisplay::reset() {
    digitalWrite(RLCD_RST, HIGH);
    delay(50);
    digitalWrite(RLCD_RST, LOW);
    delay(20);
    digitalWrite(RLCD_RST, HIGH);
    delay(50);
}

void RlcdDisplay::init(uint32_t, bool, uint16_t, bool) {
    pinMode(RLCD_CS, OUTPUT);
    pinMode(RLCD_DC, OUTPUT);
    pinMode(RLCD_RST, OUTPUT);
    digitalWrite(RLCD_CS, HIGH);
    digitalWrite(RLCD_DC, HIGH);
    _spi.begin(RLCD_SCK, -1, RLCD_MOSI, -1);
    if (!_panel) _panel = static_cast<uint8_t*>(heap_caps_malloc(PANEL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!_panel || !getBuffer()) {
        Serial.println("RLCD: no memory for the frame buffers");
        return;
    }

    reset();
    for (const InitStep& step : INIT) command(step.command, step.data, step.length);
    command(0x11);  // Sleep out
    delay(120);
    for (const InitStep& step : AFTER_WAKE) command(step.command, step.data, step.length);
    command(0x21);  // Inversion on: panel bit 1 is white, as on the canvas
    command(0x2A, COLUMNS, sizeof(COLUMNS));
    command(0x2B, ROWS, sizeof(ROWS));
    const uint8_t te = 0x00;
    command(0x35, &te, 1);          // Tearing-effect line on
    const uint8_t d0 = 0xFF;
    command(0xD0, &d0, 1);
    command(0x38);                  // High power mode
    command(0x29);                  // Display on
    _ready = true;
    clearScreen();
}

void RlcdDisplay::setPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
    _windowed = true;
    _wx0 = max<int16_t>(0, x);
    _wy0 = max<int16_t>(0, y);
    _wx1 = min<int16_t>(width(), x + w);
    _wy1 = min<int16_t>(height(), y + h);
}

void RlcdDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (inWindow(x, y)) GFXcanvas1::drawPixel(x, y, color);
}

void RlcdDisplay::fillScreen(uint16_t color) {
    if (!_windowed) GFXcanvas1::fillScreen(color);
    else if (_wx1 > _wx0 && _wy1 > _wy0) fillRect(_wx0, _wy0, _wx1 - _wx0, _wy1 - _wy0, color);
}

void RlcdDisplay::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (h < 0) { y += h + 1; h = -h; }
    if (_windowed) {
        if (x < _wx0 || x >= _wx1) return;
        const int16_t top = max(y, _wy0), bottom = min<int16_t>(y + h, _wy1);
        if (bottom <= top) return;
        y = top; h = bottom - top;
    }
    GFXcanvas1::drawFastVLine(x, y, h, color);
}

void RlcdDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (w < 0) { x += w + 1; w = -w; }
    if (_windowed) {
        if (y < _wy0 || y >= _wy1) return;
        const int16_t left = max(x, _wx0), right = min<int16_t>(x + w, _wx1);
        if (right <= left) return;
        x = left; w = right - left;
    }
    GFXcanvas1::drawFastHLine(x, y, w, color);
}

void RlcdDisplay::clearScreen(uint8_t value) {
    if (getBuffer()) memset(getBuffer(), value, PANEL_BYTES);
    flush();
}

void RlcdDisplay::refresh(bool) {
    flush();
}

// Canvas (row-major, MSB = leftmost, 1 = white) to controller order. The
// controller's memory is the panel in portrait: each byte is 4 portrait
// columns by 2 portrait rows. In canvas terms a byte covers 2 columns of 4
// rows, landscape (x, y) being portrait (299 - y, x).
void RlcdDisplay::flush() {
    if (!_ready) return;
    const uint8_t* canvas = getBuffer();
    constexpr int GROUPS = NATIVE_H / 4;  // 75 portrait column groups
    for (int group = 0; group < GROUPS; ++group) {
        const uint8_t* r0 = canvas + (NATIVE_H - 1 - 4 * group) * CANVAS_PITCH;
        const uint8_t* r1 = r0 - CANVAS_PITCH;
        const uint8_t* r2 = r1 - CANVAS_PITCH;
        const uint8_t* r3 = r2 - CANVAS_PITCH;
        uint8_t* out = _panel + group;
        for (int c = 0; c < CANVAS_PITCH; ++c) {
            const uint8_t b0 = r0[c], b1 = r1[c], b2 = r2[c], b3 = r3[c];
            for (int shift = 6; shift >= 0; shift -= 2) {
                *out = ((b0 >> shift) & 3) << 6 | ((b1 >> shift) & 3) << 4 |
                       ((b2 >> shift) & 3) << 2 | ((b3 >> shift) & 3);
                out += GROUPS;
            }
        }
    }
    command(0x2A, COLUMNS, sizeof(COLUMNS));
    command(0x2B, ROWS, sizeof(ROWS));
    command(0x2C, _panel, PANEL_BYTES);
}

void RlcdDisplay::drawGameFrame() {
    const bool nes = _nesGame;
    const int sourceW = nes ? 256 : 160;
    const int top = nes ? NES_Y : GB_Y, rows = nes ? NES_H : GB_H;
    const bool flipped = _gameRotation == 2;
    // Source column for each native column of the window.
    static uint16_t columns[GAME_W];
    for (int i = 0; i < GAME_W; ++i) {
        const int x = flipped ? GAME_W - 1 - i : i;
        columns[i] = nes ? x * 4 / 5 : x / 2;
    }
    uint8_t* canvas = getBuffer();
    for (int j = 0; j < rows; ++j) {
        const int y = flipped ? rows - 1 - j : j;
        const int ny = top + j;
        const uint8_t* source = _gameFrame + (nes ? y * 4 / 5 : y / 2) * sourceW;
        const uint8_t* threshold = THRESHOLD[ny & 1];
        uint8_t* out = canvas + ny * CANVAS_PITCH + GAME_X / 8;
        const uint16_t* column = columns;
        for (int b = 0; b < GAME_W / 8; ++b) {
            uint8_t bits = 0;
            for (int k = 0; k < 8; ++k) {
                const bool black = threshold[k & 1] < DENSITY[source[*column++] & 3];
                bits = (bits << 1) | (black ? 0 : 1);
            }
            out[b] = bits;
        }
    }
}

bool RlcdDisplay::refreshGame(bool, bool) {
    if (_gaming && _gameFrame) drawGameFrame();
    flush();
    return true;
}

void RlcdDisplay::hibernate() {
    if (!_ready) return;
    command(0x39);  // Low power mode: the panel keeps its picture
}
#endif
