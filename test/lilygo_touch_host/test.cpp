#include <assert.h>
#include <initializer_list>
#include "Wire.h"
#include "../../lib/Book32_Core/StickyTouch.h"
#include "../../lib/Book32_Core/LilygoLayout.h"

TestWire Wire;
TestSerial Serial;

int main() {
    // All logical coordinates round-trip through the physical inset for both
    // orientations; touches on the bezel margins cannot hit a control.
    for (int rotation : {1,3}) {
        for (int sy=0;sy<LilygoLayout::HEIGHT;++sy) for(int sx=0;sx<LilygoLayout::WIDTH;++sx) {
            int nx = (rotation==1 ? LilygoLayout::CANVAS_W-1-sy : sy) + LilygoLayout::INSET;
            int ny = (rotation==1 ? sx : LilygoLayout::CANVAS_H-1-sx) + LilygoLayout::INSET;
            uint16_t tx=0,ty=0;
            assert(LilygoLayout::mapTouch(nx,ny,rotation,tx,ty));
            assert(tx==sx && ty==sy);
        }
        uint16_t tx,ty;
        assert(!LilygoLayout::mapTouch(0,0,rotation,tx,ty));
        assert(!LilygoLayout::mapTouch(959,539,rotation,tx,ty));
    }
    StickyTouch touch;
    assert(touch.begin());
    uint16_t x[5] = {}, y[5] = {};
    uint8_t count = 0;
    bool home = false;
    auto read = [&](uint8_t status) {
        Wire.packet[0] = status;
        return touch.readPoints(x, y, count, home);
    };
    assert(!read(0x00) && !home); // no new packet is NOT a release
    assert(read(0x90) && home && count == 0); // very first event may be key
    assert(read(0x90) && !home); // hold, no repeated toggles
    assert(!read(0x00) && !home);
    assert(read(0x90) && !home);
    Wire.failStatus = true;
    assert(!read(0x80) && !home);
    Wire.failStatus = false;
    assert(read(0x90) && !home); // failed read did not re-arm
    Wire.failAck = true;
    assert(!read(0x80) && !home);
    Wire.failAck = false;
    assert(read(0x90) && !home);
    assert(!read(0x86) && !home); // impossible point count isn't a release
    assert(read(0x90) && !home);
    assert(read(0x80) && !home); // genuine release
    Wire.failAck = true;
    assert(!read(0x90) && !home);
    Wire.failAck = false;
    assert(read(0x90) && home); // retry acknowledged exactly once
    assert(read(0x80) && !home);

    // Ordinary screen coordinates remain unchanged (portrait -> native).
    Wire.packet[2] = 100; Wire.packet[3] = 0;
    Wire.packet[4] = 200; Wire.packet[5] = 0;
    assert(read(0x81) && !home && count == 1 && x[0] == 200 && y[0] == 439);
    Wire.failPoints = true;
    assert(!read(0x91) && !home);
    Wire.failPoints = false;
    assert(read(0x91) && home && count == 0); // bezel key never a screen tap
    assert(read(0x90) && !home);
    assert(read(0x80) && !home);
    assert(read(0x90) && home);
    puts("GT911 front key PASS: first press, hold, release, stale/error packets, ack retry, screen coordinates");
}
