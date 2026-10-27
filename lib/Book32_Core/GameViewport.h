#pragma once
#include <stdint.h>
#include <string.h>
#include "LilygoLayout.h"

#if defined(BOARD_SEEED_STICKY)
// Sticky (SSD1677) and Book32 (GD7965): 800x480 panels, 480x800 portrait UI. Game Boy at 2x in a
// 320x288 window. Each source pixel is a 2x2 cell covered 0/4, 1/4, 3/4 or 4/4
// black, so four shades need no temporal dithering.
//
// The packed frame is the window exactly as the controller RAM expects it:
// WINDOW_ROWS native rows in write order (descending native y, data entry
// mode X+ / Y-), each ROW_BYTES bytes, MSB = lowest native x, 1 = white.
namespace GameViewport {
constexpr int SOURCE_W = 160, SOURCE_H = 144, SCALE = 2;
constexpr int X = 80, Y = 64, W = SOURCE_W * SCALE, H = SOURCE_H * SCALE;
constexpr int PANEL_W = 800, PANEL_H = 480, PITCH = PANEL_W / 8;
constexpr int WINDOW_ROWS = W, ROW_BYTES = H / 8;
constexpr int PACKED_BYTES = WINDOW_ROWS * ROW_BYTES;
// Native window origin for the two portrait rotations (Adafruit GFX mapping:
// rotation 3 -> native (y, 479 - x); rotation 1 -> native (799 - y, x)).
constexpr int nativeX(uint8_t rotation) { return rotation == 3 ? Y : PANEL_W - Y - H; }
constexpr int nativeY0(uint8_t rotation) { return rotation == 3 ? PANEL_H - X - W : X; }
static_assert(X + W <= 480 && Y + H <= 800, "portrait layout");
static_assert((nativeX(1) & 7) == 0 && (nativeX(3) & 7) == 0 && (H & 7) == 0,
              "native window must be byte aligned");

// White bits for one source pixel's two native columns (dy = 0, 1) at the
// given portrait column parity dx. Coverage: 0/4, 1/4, 3/4, 4/4 black.
constexpr bool blackCell(uint8_t shade, int dx, int dy) {
    return shade == 3 || (shade >= 1 && dx == 0 && dy == 0) ||
           (shade == 2 && ((dx == 1 && dy == 0) || (dx == 1 && dy == 1)));
}
static_assert(!blackCell(0,0,0) && blackCell(1,0,0) && !blackCell(1,1,1) &&
              blackCell(2,1,1) && !blackCell(2,0,1) && blackCell(3,1,1), "four gray levels");

inline void pack(uint8_t* packed, const uint8_t* shades, uint8_t rotation) {
    if (!packed || !shades || (rotation != 1 && rotation != 3)) return;
    // pairs[dx][shade]: two white bits for the cell's dy = 0 then dy = 1.
    uint8_t pairs[2][4];
    for (int dx = 0; dx < 2; ++dx)
        for (int shade = 0; shade < 4; ++shade)
            pairs[dx][shade] = (blackCell(shade, dx, 0) ? 0 : 2) | (blackCell(shade, dx, 1) ? 0 : 1);
    for (int row = 0; row < WINDOW_ROWS; ++row) {
        // Write row `row` is portrait column lx; native columns walk portrait y.
        // bb_epaper stores canvas row y at controller RAM row 479 - y, so
        // RAM rows run opposite to GFX's native rows (verified on hardware:
        // the other order mirrors the picture left to right).
        const int lx = rotation == 3 ? W - 1 - row : row;
        const uint8_t* source = shades + (lx >> 1);
        const uint8_t* pair = pairs[lx & 1];
        uint8_t* out = packed + row * ROW_BYTES;
        for (int byte = 0; byte < ROW_BYTES; ++byte) {
            uint8_t bits = 0;
            // Four source rows per byte, two native columns each.
            for (int i = 0; i < 4; ++i) {
                const int c = byte * 8 + i * 2;  // first of the two native columns
                if (rotation == 3) {
                    // Native column c is portrait ly = c: dy order 0 then 1.
                    bits = (bits << 2) | pair[source[(c >> 1) * SOURCE_W] & 3];
                } else {
                    // Native column c is portrait ly = H - 1 - c: dy order 1 then 0.
                    const uint8_t p = pair[source[((H - 1 - c) >> 1) * SOURCE_W] & 3];
                    bits = (bits << 2) | ((p >> 1) | ((p & 1) << 1));
                }
            }
            out[byte] = bits;
        }
    }
}
}
#elif defined(BOARD_WAVESHARE_RLCD42)
// RLCD: the frame is the 160x144 shades as they are; RlcdDisplay scales it
// 2x into a 320x288 window of the 400x300 landscape panel and dithers it.
namespace GameViewport {
constexpr int SOURCE_W = 160, SOURCE_H = 144, SCALE = 2;
constexpr int X = 40, Y = 6, W = SOURCE_W * SCALE, H = SOURCE_H * SCALE;
constexpr int PACKED_BYTES = SOURCE_W * SOURCE_H;
inline void pack(uint8_t* packed, const uint8_t* shades, uint8_t) {
    if (packed && shades) memcpy(packed, shades, PACKED_BYTES);
}
}
#else
namespace GameViewport {
constexpr int SOURCE_W = 160, SOURCE_H = 144, SCALE = 3;
constexpr int X = 22, Y = 72, W = SOURCE_W * SCALE, H = SOURCE_H * SCALE;
constexpr int PANEL_W = LilygoLayout::CANVAS_W, PANEL_H = LilygoLayout::CANVAS_H, PITCH = PANEL_W / 8;
constexpr int ROW_BYTES = H / 8;
constexpr int PACKED_BYTES = SOURCE_W * ROW_BYTES;
constexpr int nativeX(uint8_t rotation) { return rotation == 3 ? Y : PANEL_W - Y - H; }
constexpr int nativeY(uint8_t rotation) { return rotation == 3 ? PANEL_H - X - W : X; }
// 3x3 cells: 0, 1, 2 or 3 black horizontal stripes, preserving four shades
// without checkerboard interpolation. Peanut-GB shade 0 is white, 3 is black.
constexpr bool blackStripe(uint8_t shade, unsigned stripe) { return stripe < (shade & 3); }
static_assert(W == 480 && H == 432 && Y + H <= 510, "portrait layout");
static_assert((nativeX(1) & 7) == 0 && (nativeX(3) & 7) == 0 && (H & 7) == 0,
              "native viewport must be byte aligned for exact clipping");
static_assert(nativeY(1) == 22 && nativeY(3) == 22, "centered both orientations");
static_assert(!blackStripe(0,0) && blackStripe(1,0) && !blackStripe(1,1) &&
              blackStripe(2,1) && !blackStripe(2,2) && blackStripe(3,2), "four gray levels");

inline void packRow(uint8_t* row, const uint8_t* shades, int nativeRow, uint8_t rotation) {
    const int sx = rotation == 3 ? SOURCE_W - 1 - nativeRow : nativeRow;
    // Eight source pixels become exactly three bytes of 3x stripe shading.
    // No per-pixel destination read/modify/write, and no duplicated rows in RAM.
    const uint8_t forward[4] = {7, 3, 1, 0}, reverse[4] = {7, 6, 4, 0};
    const uint8_t* stripe = rotation == 3 ? forward : reverse;
    for (int col = 0; col < SOURCE_H; col += 8) {
        uint32_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            const int sy = rotation == 3 ? col + i : SOURCE_H - 1 - col - i;
            bits = (bits << 3) | stripe[shades[sy * SOURCE_W + sx] & 3];
        }
        *row++ = bits >> 16; *row++ = bits >> 8; *row++ = bits;
    }
}
inline void pack3x(uint8_t* packed, const uint8_t* shades, uint8_t rotation) {
    if (!packed || !shades || (rotation != 1 && rotation != 3)) return;
    for (int row = 0; row < SOURCE_W; ++row)
        packRow(packed + row * ROW_BYTES, shades, row, rotation);
}
inline void pack(uint8_t* packed, const uint8_t* shades, uint8_t rotation) { pack3x(packed, shades, rotation); }
inline void blitPacked(uint8_t* canvas, const uint8_t* packed, uint8_t rotation) {
    if (!canvas || !packed || (rotation != 1 && rotation != 3)) return;
    for (int row = 0; row < SOURCE_W; ++row) for (int repeat = 0; repeat < SCALE; ++repeat)
        memcpy(canvas + (nativeY(rotation) + row * SCALE + repeat) * PITCH + nativeX(rotation) / 8,
               packed + row * ROW_BYTES, ROW_BYTES);
}
inline void blit3x(uint8_t* canvas, const uint8_t* shades, uint8_t rotation) {
    if (!canvas || !shades || (rotation != 1 && rotation != 3)) return;
    uint8_t row[ROW_BYTES];
    for (int y = 0; y < SOURCE_W; ++y) {
        packRow(row, shades, y, rotation);
        for (int repeat = 0; repeat < SCALE; ++repeat)
            memcpy(canvas + (nativeY(rotation) + y * SCALE + repeat) * PITCH + nativeX(rotation) / 8,
                   row, ROW_BYTES);
    }
}
}
#endif
