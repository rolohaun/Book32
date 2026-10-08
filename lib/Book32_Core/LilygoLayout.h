#pragma once
#include <stdint.h>

// Keep UI pixels clear of the bezel without scaling text or the 3x game image.
namespace LilygoLayout {
constexpr int INSET = 8;
constexpr int PANEL_W = 960, PANEL_H = 540;
constexpr int CANVAS_W = PANEL_W - 2 * INSET, CANVAS_H = PANEL_H - 2 * INSET;
constexpr int WIDTH = CANVAS_H, HEIGHT = CANVAS_W;
constexpr int PITCH = CANVAS_W / 8;
static_assert((CANVAS_W & 7) == 0 && (INSET & 7) == 0, "byte-aligned canvas");
inline bool mapTouch(uint16_t nx, uint16_t ny, uint8_t rotation, uint16_t& x, uint16_t& y) {
    if (nx >= PANEL_W || ny >= PANEL_H || (rotation != 1 && rotation != 3)) return false;
    int px = rotation == 1 ? ny : PANEL_H - 1 - ny;
    int py = rotation == 1 ? PANEL_W - 1 - nx : nx;
    if (px < INSET || px >= PANEL_H - INSET || py < INSET || py >= PANEL_W - INSET) return false;
    x = px - INSET; y = py - INSET;
    return true;
}
}
