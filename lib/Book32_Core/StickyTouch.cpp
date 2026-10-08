#include "Config.h"
#include "StickyTouch.h"

#if BOOK32_HAS_TOUCH

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
#if defined(BOARD_SEEED_STICKY)
    pinMode(TOUCH_ENABLE, OUTPUT);
    digitalWrite(TOUCH_ENABLE, HIGH);
    delay(50);

    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    Wire.setTimeOut(10);
#endif

    _address = 0;
#if defined(BOARD_LILYGO_T5S3_PRO)
    _homeKeyDown = false;
#endif
    resetWithInterruptLevel(LOW);
    if (!probe()) {
        resetWithInterruptLevel(HIGH);
        probe();
    }
    if (_address) Serial.printf("Sticky touch: GT911 ready at 0x%02X\n", _address);
    else Serial.println("Sticky touch: GT911 not found");
    return _address != 0;
}

void StickyTouch::stop() {
    // Stop all I2C traffic before the touch rail is removed. Holding reset low
    // also prevents the unpowered GT911 from being back-fed through a signal.
    _address = 0;
#if defined(BOARD_SEEED_STICKY)
    Wire.end();
#endif
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
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
#if defined(BOARD_LILYGO_T5S3_PRO)
            // H752-01 GT911 reports portrait 540x960, not panel 960x540.
            nativeX = constrain(rawY, 0, PANEL_WIDTH - 1);
            nativeY = PANEL_HEIGHT - 1 - constrain(rawX, 0, PANEL_HEIGHT - 1);
#else
            nativeX = static_cast<uint16_t>(799 - constrain(rawY, 0, 799));
            nativeY = static_cast<uint16_t>(479 - constrain(rawX, 0, 479));
#endif
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

#if defined(BOARD_LILYGO_T5S3_PRO)
bool StickyTouch::readPoints(uint16_t* x, uint16_t* y, uint8_t& count, bool& homePressed) {
    homePressed = false;
    uint8_t status = 0;
    if (!readRegister(0x814E, &status, 1) || !(status & 0x80)) return false;
    count = status & 15;
    uint8_t data[40] = {};
    bool valid = count <= 5 && (!count || readRegister(0x814F, data, count * 8));
    const bool acknowledged = writeRegister(0x814E, 0);
    if (!valid || !acknowledged) return false;
    // The front-bottom capacitive button is GT911's HaveKey bit, not the
    // separate PCA9535 S3 switch. Consume it before discarding the status.
    // Only fresh, fully read/acknowledged packets change the latch: an idle
    // register read or I2C failure must not re-arm a held key.
    const bool homeDown = (status & 0x10) != 0;
    homePressed = homeDown && !_homeKeyDown;
    _homeKeyDown = homeDown;
    if (homeDown) {
        count = 0; // Never turn the bezel button into an on-screen app tap.
        return true;
    }
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t rawX = data[i * 8 + 1] | (data[i * 8 + 2] << 8);
        uint16_t rawY = data[i * 8 + 3] | (data[i * 8 + 4] << 8);
        // Convert portrait digitizer coordinates into the native canvas;
        // DisplayMgr then applies the user's selected portrait rotation.
        if (rawX >= PANEL_HEIGHT || rawY >= PANEL_WIDTH) {
            x[i] = PANEL_WIDTH; y[i] = PANEL_HEIGHT; // rejected by DisplayMgr
        } else { x[i] = rawY; y[i] = PANEL_HEIGHT - 1 - rawX; }
    }
    return true;
}
#endif

#endif
