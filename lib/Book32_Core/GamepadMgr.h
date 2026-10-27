#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "HidGamepad.h"

// Bluetooth LE gamepads (HID over GATT) on every InkDeck board. The ESP32-S3
// has Bluetooth LE only, so a pad must offer a BLE mode (Classic-only pads,
// such as many in "Switch" or older Android modes, cannot connect).
//
// The radio starts only when a pad is saved or a scan is requested. A saved
// pad reconnects automatically in the background. Menus get Next / Previous /
// Select / Back from the pad; Inkade reads the raw buttons for games.
class GamepadMgr {
public:
    struct Found { String name, address; uint8_t addressType; int rssi; bool gamepad; };
    enum State : uint8_t { OFF, IDLE, SCANNING, CONNECTING, CONNECTED };

    static GamepadMgr& getInstance();

    // Starts the background task (and the radio when a pad is saved).
    void begin();
    // Stops the radio before deep sleep.
    void shutdown();

    void scan(uint32_t seconds = 8);
    std::vector<Found> found();
    // Saves the pad and connects to it (now and after every restart).
    void pair(const String& address, uint8_t addressType, const String& name);
    void forget();

    State state() const { return _state.load(); }
    const char* stateName() const;
    String savedName();
    String savedAddress();
    bool hasSaved() { return savedAddress().length() > 0; }
    uint8_t battery() const { return _battery.load(); }

    // Decoded buttons (HidGamepad::PAD_*), 0 when disconnected.
    uint16_t buttons() const { return _state == CONNECTED ? _buttons.load() : 0; }
    // Buttons from everything but keyboard keys (games map keys separately).
    uint16_t padButtons() const { return _state == CONNECTED ? _padOnly.load() : 0; }
    // Keyboard keys held now (none when disconnected or not a keyboard).
    HidGamepad::Keys keys();
    // The connected device has keyboard keys (Controls then maps keys).
    bool isKeyboard() const { return _state == CONNECTED && _keyboard.load(); }
    // Raw HID button bits of the last report (for diagnostics and mapping).
    uint32_t rawButtons() const { return _raw.load(); }
    unsigned long lastInputAt() const { return _lastInputAt.load(); }

    // Games read raw buttons; menus get key actions (with auto-repeat).
    void setRawMode(bool raw) { _rawMode = raw; }
    bool rawMode() const { return _rawMode; }
    // Called from the main loop: emits menu actions for new presses.
    // `action` receives 1 next, 2 previous, 3 select, 4 back, 5 home.
    void pollActions(const std::function<void(int)>& action);

    // Serial commands ("bt scan", "bt pair <address> <type>", "bt forget",
    // "bt status"). Returns true when the line was a bt command.
    bool serialCommand(const char* line);

    // Called by the BLE callbacks.
    void onReport(uint8_t reportId, const uint8_t* data, size_t length);
    void onFound(const Found& device);
    void onDisconnected();

private:
    GamepadMgr() = default;
    static void taskEntry(void* self);
    void task();
    bool startRadio();
    bool connectSaved();

    std::atomic<State> _state{OFF};
    std::atomic<uint16_t> _buttons{0}, _padOnly{0};
    std::atomic<bool> _keyboard{false};
    portMUX_TYPE _keysLock = portMUX_INITIALIZER_UNLOCKED;
    HidGamepad::Keys _keys;
    std::atomic<uint32_t> _raw{0};
    std::atomic<uint8_t> _battery{0};
    std::atomic<unsigned long> _lastInputAt{0};
    std::atomic<bool> _scanRequested{false}, _connectRequested{false}, _forgetRequested{false};
    std::atomic<bool> _stopping{false};
    uint32_t _scanSeconds = 8;
    bool _rawMode = false;
    bool _radioOn = false;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _lock = nullptr;
    std::vector<Found> _found;
    HidGamepad::Descriptor _descriptor;
    bool _reportLogged = false;
    uint16_t _padByReport[256] = {};
    uint32_t _rawByReport[256] = {};
    uint16_t _otherByReport[256] = {};
    HidGamepad::Keys _keysByReport[4];  // keyboards use few report IDs: by id & 3
    String _saved;
    bool _savedLoaded = false;
    // Menu action state (main loop only).
    uint16_t _lastButtons = 0;
    unsigned long _repeatAt = 0;
    uint16_t _repeatButton = 0;
};
