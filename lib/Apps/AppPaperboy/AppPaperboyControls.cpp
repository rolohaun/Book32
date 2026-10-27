// Inkade's Controls screen: which gamepad buttons press each game button.
// The mapping lives in NVS, so it needs no card and survives updates.
#include "AppPaperboy.h"
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY)
#include <Preferences.h>
#include "DisplayMgr.h"
#include "GamepadMgr.h"
#include "InkBoyLibraryUi.h"

namespace {
constexpr const char* PREFS = "inkade";
constexpr const char* KEY = "padmap";
constexpr const char* KEYBOARD_KEY = "keymap";
}

void AppPaperboy::loadPadMap() {
    _padMap = PadMapping::DEFAULTS;
    Preferences prefs;
    if (!prefs.begin(PREFS, true)) return;  // nothing saved yet
    uint8_t stored[PadMapping::STORED_BYTES];
    const size_t length = prefs.getBytesLength(KEY);
    if (length == sizeof(stored) && prefs.getBytes(KEY, stored, sizeof(stored)) == sizeof(stored) &&
        !PadMapping::restore(_padMap, stored, sizeof(stored)))
        Serial.println("Inkade: stored gamepad mapping is from an older version or unreadable; using defaults");
    _keyMap = KeyMapping::DEFAULTS;
    uint8_t keys[KeyMapping::STORED_BYTES];
    const size_t keyBytes = prefs.getBytesLength(KEYBOARD_KEY);
    if (keyBytes && keyBytes <= sizeof(keys) && prefs.getBytes(KEYBOARD_KEY, keys, keyBytes) == keyBytes &&
        !KeyMapping::restore(_keyMap, keys, keyBytes))
        Serial.println("Inkade: stored keyboard mapping is unreadable; using defaults");
    prefs.end();
}

void AppPaperboy::savePadMap() {
    uint8_t stored[PadMapping::STORED_BYTES];
    PadMapping::store(_padMap, stored);
    Preferences prefs;
    bool ok = prefs.begin(PREFS, false);
    if (ok) {
        ok = PadMapping::isDefault(_padMap) ? (prefs.isKey(KEY) ? prefs.remove(KEY) : true)
                                            : prefs.putBytes(KEY, stored, sizeof(stored)) == sizeof(stored);
        uint8_t keys[KeyMapping::STORED_BYTES];
        KeyMapping::store(_keyMap, keys);
        ok &= KeyMapping::isDefault(_keyMap) ? (prefs.isKey(KEYBOARD_KEY) ? prefs.remove(KEYBOARD_KEY) : true)
                                             : prefs.putBytes(KEYBOARD_KEY, keys, sizeof(keys)) == sizeof(keys);
        prefs.end();
    }
    if (!ok) _controlsMessage = "Could not save the controls";
}

bool AppPaperboy::keyboardControls() const { return GamepadMgr::getInstance().isKeyboard(); }

String AppPaperboy::controlValue(int action) const {
    if (keyboardControls()) {
        char buffer[12];
        return HidGamepad::keyName(_keyMap.keys[action], buffer);
    }
    char value[64];
    PadMapping::describe(_padMap.masks[action], value, sizeof(value));
    return value;
}

void AppPaperboy::openControls() {
    _controls = true;
    _controlsFull = true;
    _controlsCursor = -1;
    _capture = -1;
    _controlsMessage = "";
    _redraw = true;
}

void AppPaperboy::paintControls() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false);
    d.setFullWindow();
    String values[PadMapping::ACTION_COUNT];
    const char* shown[PadMapping::ACTION_COUNT];
    for (int i = 0; i < PadMapping::ACTION_COUNT; ++i) { values[i] = controlValue(i); shown[i] = values[i].c_str(); }
    InkBoyLibraryUi::controls(d, shown, keyboardControls(), _controlsCursor, _capture, _controlsMessage.c_str());
    d.refresh(!_controlsFull);
    _controlsFull = false;
}

void AppPaperboy::activateControlRow(int row) {
    _controlsCursor = row;
    if (row == PadMapping::ACTION_COUNT) {
        // Resets the device in use: the keyboard's keys or the pad's buttons.
        if (keyboardControls()) _keyMap = KeyMapping::DEFAULTS;
        else _padMap = PadMapping::DEFAULTS;
        _controlsMessage = keyboardControls() ? "Keyboard keys reset to defaults" : "Controls reset to defaults";
        savePadMap();
    } else if (GamepadMgr::getInstance().state() != GamepadMgr::CONNECTED) {
        _controlsMessage = "Connect a gamepad in InkDeck's Settings first";
    } else {
        _capture = row;
        _captureArmed = false;  // the press that chose the row does not count
        _controlsMessage = "";
    }
    _redraw = true;
}

void AppPaperboy::controlsBack() {
    if (_capture >= 0) {
        _capture = -1;
        _controlsMessage = "";
        _redraw = true;
        return;
    }
    _controls = false;
    _redraw = true;
}

void AppPaperboy::controlsTap(uint16_t x, uint16_t y) {
    if (y < 60) { controlsBack(); return; }
    const int row = InkBoyLibraryUi::controlRowAt(x, y, InkBoyLibraryUi::CONTROL_ROWS);
    if (row < 0) {
        if (_capture >= 0) controlsBack();  // a tap elsewhere cancels waiting
        return;
    }
    activateControlRow(row);
}

// Board keys, and the gamepad's menu actions when it is not being captured.
void AppPaperboy::controlsKey(InputAction a) {
    if (_capture >= 0) {
        if (a == INPUT_BACK) controlsBack();
        return;
    }
    constexpr int ROWS = InkBoyLibraryUi::CONTROL_ROWS;
    if (a == INPUT_NEXT || a == INPUT_PREV) {
        _controlsCursor = _controlsCursor < 0 ? 0 : (_controlsCursor + (a == INPUT_NEXT ? 1 : ROWS - 1)) % ROWS;
        _redraw = true;
    } else if (a == INPUT_SELECT) {
        if (_controlsCursor < 0) { _controlsCursor = 0; _redraw = true; }
        else activateControlRow(_controlsCursor);
    } else if (a == INPUT_BACK) {
        controlsBack();
    }
}

// Called every update while the screen is open: takes the captured press.
void AppPaperboy::updateControls() {
    GamepadMgr& pad = GamepadMgr::getInstance();
    const HidGamepad::Keys keys = pad.keys();
    // A keyboard is captured by key, a gamepad by button.
    const bool keyboard = keyboardControls();
    const uint16_t held = keyboard ? keys.count : pad.buttons() & PadMapping::ALL_BUTTONS;
    if (_captureRelease && !held) _captureRelease = false;
    if (_capture < 0) return;
    if (pad.state() != GamepadMgr::CONNECTED) {
        _capture = -1;
        _controlsMessage = "Gamepad disconnected";
        _redraw = true;
        return;
    }
    if (!_captureArmed) {
        _captureArmed = !held;
        return;
    }
    if (!held) return;
    const int action = _capture;
    const int swapped = keyboard ? KeyMapping::assign(_keyMap, action, keys.codes[0])
                                 : PadMapping::assign(_padMap, action, held);
    auto now = [&](int a) { return String(PadMapping::actionName(a)) + " is now " + controlValue(a); };
    _controlsMessage = now(action);
    if (swapped >= 0) _controlsMessage += "; " + now(swapped) + " (swapped)";
    savePadMap();
    Serial.printf("Inkade: %s\n", _controlsMessage.c_str());
    _capture = -1;
    _captureRelease = true;  // keep the press away from the menu actions
    _redraw = true;
}
#endif
