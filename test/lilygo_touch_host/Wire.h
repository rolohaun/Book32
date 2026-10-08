#pragma once
#include <stdint.h>
#include <string.h>
#include <assert.h>

// Mock only the GT911 transactions used by the real StickyTouch driver.
struct TestWire {
    uint8_t packet[41] = {}; // status + five eight-byte points
    bool failStatus = false, failPoints = false, failAck = false;
    uint16_t reg = 0;
    uint8_t writes[3] = {}, written = 0, readAt = 0, readSize = 0;
    void beginTransmission(uint8_t) { written = 0; }
    void write(uint8_t value) { assert(written < 3); writes[written++] = value; }
    uint8_t endTransmission(bool = true) {
        if (!written) return 0; // address probe
        assert(written >= 2);
        reg = (uint16_t(writes[0]) << 8) | writes[1];
        if (written == 3) {
            assert(reg == 0x814e && writes[2] == 0);
            if (failAck) return 1;
            packet[0] = 0;
        }
        return 0;
    }
    uint8_t requestFrom(uint8_t, uint8_t length, uint8_t) {
        assert(reg == 0x814e || reg == 0x814f);
        readAt = 0;
        readSize = ((reg == 0x814e && failStatus) || (reg == 0x814f && failPoints)) ? 0 : length;
        return readSize;
    }
    int available() { return readSize - readAt; }
    uint8_t read() {
        assert(readAt < readSize);
        return packet[(reg - 0x814e) + readAt++];
    }
};
extern TestWire Wire;
