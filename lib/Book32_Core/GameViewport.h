#pragma once
#include <stdint.h>
#include <string.h>
#include "LilygoLayout.h"

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
