#include "StickyTouch.h"

#if defined(BOARD_SEEED_STICKY)

#include "Config.h"
#include <Wire.h>

void StickyTouch::resetWithInterruptLevel(uint8_t level) {
    pinMode(TOUCH_INT, OUTPUT);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    digitalWrite(TOUCH_INT, level);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(10);
    digitalWrite(TOUCH_INT, level);
    delay(50);
    pinMode(TOUCH_INT, INPUT);
    delay(50);
}

bool StickyTouch::probe() {
    const uint8_t candidates[] = {0x5D, 0x14};
    for (uint8_t address : candidates) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            _address = address;
            return true;
        }
    }
    return false;
}

bool StickyTouch::begin() {
    pinMode(TOUCH_ENABLE, OUTPUT);
    digitalWrite(TOUCH_ENABLE, HIGH);
    delay(50);

    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    Wire.setTimeOut(10);

    _address = 0;
    resetWithInterruptLevel(LOW);
    if (!probe()) {
        resetWithInterruptLevel(HIGH);
        probe();
    }
    if (_address) Serial.printf("Sticky touch: GT911 ready at 0x%02X\n", _address);
    else Serial.println("Sticky touch: GT911 not found");
    return _address != 0;
}

bool StickyTouch::readRegister(uint16_t reg, uint8_t* data, uint8_t length) {
    if (!_address) return false;
    Wire.beginTransmission(_address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(_address, length, static_cast<uint8_t>(true)) != length) {
        while (Wire.available()) Wire.read();
        return false;
    }
    for (uint8_t i = 0; i < length; ++i) data[i] = Wire.read();
    return true;
}

bool StickyTouch::writeRegister(uint16_t reg, uint8_t value) {
    if (!_address) return false;
    Wire.beginTransmission(_address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool StickyTouch::readFrame(bool& touching, uint16_t& nativeX, uint16_t& nativeY) {
    if (!_address) return false;
    uint8_t status = 0;
    if (!readRegister(0x814E, &status, 1) || !(status & 0x80)) return false;

    bool valid = true;
    uint8_t count = status & 0x0F;
    if (count > 0) {
        uint8_t point[8] = {};
        if (readRegister(0x8150, point, sizeof(point))) {
            uint16_t rawX = static_cast<uint16_t>(point[0]) |
                            (static_cast<uint16_t>(point[1]) << 8);
            uint16_t rawY = static_cast<uint16_t>(point[2]) |
                            (static_cast<uint16_t>(point[3]) << 8);
            // The Sticky digitizer is mounted portrait relative to the panel:
            // swap axes, then flip both native panel axes.
            nativeX = static_cast<uint16_t>(799 - constrain(rawY, 0, 799));
            nativeY = static_cast<uint16_t>(479 - constrain(rawX, 0, 479));
            touching = true;
        } else {
            valid = false;
        }
    } else {
        touching = false;
    }

    writeRegister(0x814E, 0);
    return valid;
}

#endif
