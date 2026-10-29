#pragma once
#include "DisplayMgr.h"

// The bottom of a home screen (main menu or bookshelf): two lines in each
// corner, 22 px from the edges. InkDeck's version over the battery on the
// left; the web page's address under "Dashboard" on the right. Without
// `details` only the battery is drawn (the library inside the app menu).
namespace HomeFooter {
constexpr int MARGIN = 22;
constexpr int HEIGHT = 50;  // from the upper line's tallest letter to the bottom
void draw(Book32Display& display, bool details);
// A compact battery: four upright bars, one filled per quarter of charge,
// with the percentage to their right. `baseline` is the text's baseline.
void drawBattery(Book32Display& display, int x, int baseline);
}
