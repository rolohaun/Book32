#pragma once

#if defined(BOARD_SEEED_STICKY)

#include <Arduino.h>

class StickyTouch {
public:
    bool begin();
    bool readFrame(bool& touching, uint16_t& nativeX, uint16_t& nativeY);
    bool available() const { return _address != 0; }

private:
    uint8_t _address = 0;

    bool probe();
    bool readRegister(uint16_t reg, uint8_t* data, uint8_t length);
    bool writeRegister(uint16_t reg, uint8_t value);
    void resetWithInterruptLevel(uint8_t level);
};

#endif
