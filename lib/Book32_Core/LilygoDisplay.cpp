#include "LilygoDisplay.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Wire.h>

void LilygoDisplay::init(uint32_t, bool, uint16_t, bool) {
    pinMode(11, OUTPUT); // Frontlight off until explicitly requested.
    digitalWrite(11, LOW);
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    Wire.setTimeOut(20);
    // Do not energize a panel using the older H752's incompatible pinout.
    Wire.beginTransmission(0x20);
    if (Wire.endTransmission() != 0) {
        Serial.println("LILYGO H752-01 expander missing; display disabled");
        return;
    }
    // Official H752-01 examples use the EPDiy V7 parallel/power mapping.
    _ready = _panel.initPanel(BB_PANEL_EPDIY_V7, 20000000) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setPanelSize(960, 540) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setMode(BB_MODE_1BPP) == BBEP_SUCCESS;
    if (_ready) clearScreen();
}

void LilygoDisplay::refresh(bool partial) {
    if (!_ready || !getBuffer() || !_panel.currentBuffer()) return;
    // Both buffers are MSB-first, native-landscape, white=1.
    memcpy(_panel.currentBuffer(), getBuffer(), 960 * 540 / 8);
    int result = partial ? _panel.partialUpdate(_gaming)
                         : _panel.fullUpdate(CLEAR_SLOW, false);
    if (result != BBEP_SUCCESS) {
        Serial.printf("Parallel display error %d; disabling panel\n", result);
        _panel.einkPower(0);
        _ready = false;
    }
}

void LilygoDisplay::clearScreen(uint8_t value) {
    if (getBuffer()) memset(getBuffer(), value, 960 * 540 / 8);
    refresh(false);
}

void LilygoDisplay::setGameMode(bool active) {
    _gaming = active;
    // Separate from reader settings: a single differential pass for animation.
    _panel.setPasses(active ? 1 : 4);
    if (!active && _ready) _panel.einkPower(0);
}

void LilygoDisplay::hibernate() {
    if (_ready) _panel.einkPower(0);
    digitalWrite(11, LOW);
}
#endif
