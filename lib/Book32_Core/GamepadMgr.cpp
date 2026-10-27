#include "GamepadMgr.h"

#include <NimBLEDevice.h>
#include <Preferences.h>

namespace {
const NimBLEUUID HID_SERVICE(static_cast<uint16_t>(0x1812));
const NimBLEUUID REPORT_MAP(static_cast<uint16_t>(0x2A4B));
const NimBLEUUID REPORT(static_cast<uint16_t>(0x2A4D));
const NimBLEUUID REPORT_REFERENCE(static_cast<uint16_t>(0x2908));
const NimBLEUUID BATTERY_SERVICE(static_cast<uint16_t>(0x180F));
const NimBLEUUID BATTERY_LEVEL(static_cast<uint16_t>(0x2A19));
constexpr uint32_t RECONNECT_MS = 4000;
constexpr unsigned long REPEAT_DELAY_MS = 400, REPEAT_MS = 150;
constexpr const char* PREFS = "gamepad";

NimBLEClient* client = nullptr;
// Input report characteristic handle -> report ID (from its Report Reference).
struct ReportSlot { uint16_t handle; uint8_t id; };
ReportSlot slots[8];
int slotCount = 0;

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override {
        const bool hid = d->isAdvertisingService(HID_SERVICE);
        const uint16_t appearance = d->haveAppearance() ? d->getAppearance() : 0;
        // Appearance 0x03C0..0x03C4: generic HID, keyboard, mouse, joystick,
        // gamepad. Keyboards work like a pad (HidGamepad::keyButton).
        const bool pad = hid || appearance == 0x03C1 || (appearance >= 0x03C3 && appearance <= 0x03C4);
        if (!pad && !d->haveName()) return;
        GamepadMgr::Found found;
        found.name = d->haveName() ? String(d->getName().c_str()) : String("");
        found.address = String(d->getAddress().toString().c_str());
        found.addressType = d->getAddress().getType();
        found.rssi = d->getRSSI();
        found.gamepad = pad;
        GamepadMgr::getInstance().onFound(found);
    }
} scanCallbacks;

class ClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*) override { GamepadMgr::getInstance().onDisconnected(); }
} clientCallbacks;

// Encrypts the link, waiting at most `ms`. NimBLE's secureConnection() waits
// forever, and pads that start pairing themselves (8BitDo) can leave it hung.
bool encryptLink(uint32_t ms) {
    const int rc = NimBLEDevice::startSecurity(client->getConnId());
    if (rc != 0 && rc != BLE_HS_EALREADY) return false;
    for (const unsigned long start = millis(); millis() - start < ms; delay(50)) {
        if (!client->isConnected()) return false;
        if (client->getConnInfo().isEncrypted()) return true;
    }
    return false;
}

void onNotify(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool) {
    uint8_t id = 0;
    for (int i = 0; i < slotCount; ++i)
        if (slots[i].handle == characteristic->getHandle()) id = slots[i].id;
    GamepadMgr::getInstance().onReport(id, data, length);
}
}

GamepadMgr& GamepadMgr::getInstance() {
    static GamepadMgr instance;
    return instance;
}

const char* GamepadMgr::stateName() const {
    switch (_state.load()) {
    case OFF: return "off";
    case IDLE: return "idle";
    case SCANNING: return "scanning";
    case CONNECTING: return "connecting";
    case CONNECTED: return "connected";
    }
    return "?";
}

String GamepadMgr::savedName() {
    Preferences prefs;
    prefs.begin(PREFS, true);
    String name = prefs.getString("name", "");
    prefs.end();
    return name;
}

String GamepadMgr::savedAddress() {
    if (!_savedLoaded) {
        Preferences prefs;
        prefs.begin(PREFS, true);
        _saved = prefs.getString("addr", "");
        prefs.end();
        _savedLoaded = true;
    }
    return _saved;
}

void GamepadMgr::begin() {
    if (_task) return;
    _lock = xSemaphoreCreateMutex();
    _stopping = false;
    // Core 0 alongside the BLE host; low priority, it mostly sleeps.
    xTaskCreatePinnedToCore(taskEntry, "Gamepad", 6144, this, 1, &_task, 0);
}

void GamepadMgr::shutdown() {
    if (!_task) return;
    _stopping = true;
    for (int i = 0; i < 60 && _task; ++i) delay(50);
}

bool GamepadMgr::startRadio() {
    if (_radioOn) return true;
    NimBLEDevice::init("InkDeck");
    NimBLEDevice::setPower(ESP_PWR_LVL_P6);
    // Bond without a passkey: gamepads have no display or keypad.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    _radioOn = true;
    if (_state == OFF) _state = IDLE;
    Serial.println("Gamepad: Bluetooth LE on");
    return true;
}

void GamepadMgr::scan(uint32_t seconds) {
    _scanSeconds = seconds ? seconds : 8;
    _scanRequested = true;
}

std::vector<GamepadMgr::Found> GamepadMgr::found() {
    std::vector<Found> copy;
    if (_lock && xSemaphoreTake(_lock, pdMS_TO_TICKS(200))) {
        copy = _found;
        xSemaphoreGive(_lock);
    }
    return copy;
}

void GamepadMgr::onFound(const Found& device) {
    if (!_lock || !xSemaphoreTake(_lock, pdMS_TO_TICKS(50))) return;
    bool known = false;
    for (Found& f : _found)
        if (f.address == device.address) {
            if (device.name.length()) f.name = device.name;
            f.rssi = device.rssi;
            f.gamepad |= device.gamepad;
            known = true;
        }
    if (!known && _found.size() < 24) {
        _found.push_back(device);
        Serial.printf("Gamepad: found %s \"%s\" type %u rssi %d%s\n", device.address.c_str(), device.name.c_str(),
                      device.addressType, device.rssi, device.gamepad ? " [HID]" : "");
    }
    xSemaphoreGive(_lock);
}

void GamepadMgr::pair(const String& address, uint8_t addressType, const String& name) {
    Preferences prefs;
    prefs.begin(PREFS, false);
    prefs.putString("addr", address);
    prefs.putUChar("type", addressType);
    prefs.putString("name", name);
    prefs.end();
    _saved = address;
    _savedLoaded = true;
    Serial.printf("Gamepad: saved %s (%s)\n", address.c_str(), name.c_str());
    _connectRequested = true;
}

void GamepadMgr::forget() { _forgetRequested = true; }

bool GamepadMgr::connectSaved() {
    Preferences prefs;
    prefs.begin(PREFS, true);
    const String address = prefs.getString("addr", "");
    const uint8_t type = prefs.getUChar("type", 0);
    prefs.end();
    if (!address.length() || !startRadio()) return false;

    _state = CONNECTING;
    if (!client) {
        client = NimBLEDevice::createClient();
        client->setClientCallbacks(&clientCallbacks, false);
        client->setConnectTimeout(4);
        // 7.5-15 ms intervals keep input latency low.
        client->setConnectionParams(6, 12, 0, 300);
    }
    const NimBLEAddress target(std::string(address.c_str()), type);
    if (!client->connect(target)) {
        _state = IDLE;
        return false;
    }
    Serial.printf("Gamepad: connected to %s\n", address.c_str());
    // Most pads require an encrypted link before they send input.
    if (!encryptLink(10000)) Serial.println("Gamepad: pairing/encryption failed (continuing)");
    if (!client->isConnected()) {
        _state = IDLE;
        return false;
    }

    NimBLERemoteService* hid = client->getService(HID_SERVICE);
    if (!hid) {
        Serial.println("Gamepad: no HID service; not a gamepad? Its services:");
        // Diagnostics: list what the device does offer.
        if (std::vector<NimBLERemoteService*>* services = client->getServices(true)) {
            for (NimBLERemoteService* s : *services) {
                Serial.printf("  service %s\n", s->getUUID().toString().c_str());
                if (std::vector<NimBLERemoteCharacteristic*>* cs = s->getCharacteristics(true))
                    for (NimBLERemoteCharacteristic* c : *cs)
                        Serial.printf("    char %s%s%s%s%s\n", c->getUUID().toString().c_str(),
                                      c->canRead() ? " read" : "", c->canWrite() ? " write" : "",
                                      c->canWriteNoResponse() ? " writeNR" : "", c->canNotify() ? " notify" : "");
            }
        }
        // Pads in a vendor/app mode (e.g. EasySMX D05 in Switch mode) offer only
        // these. Their gamepad mode (D05: X + Home, "Xbox Wireless Controller")
        // has HID.
        client->disconnect();
        _state = IDLE;
        return false;
    }
    NimBLERemoteCharacteristic* map = hid->getCharacteristic(REPORT_MAP);
    NimBLEAttValue descriptor;
    if (map) descriptor = map->readValue();
    if (!_descriptor.parse(descriptor.data(), descriptor.size())) {
        Serial.printf("Gamepad: report map unreadable (%u bytes)\n", unsigned(descriptor.size()));
    } else {
        bool keyboard = false;
        for (int i = 0; i < _descriptor.count; ++i) keyboard |= _descriptor.fields[i].usagePage == HidGamepad::PAGE_KEYBOARD;
        _keyboard = keyboard;
        Serial.printf("Gamepad: report map %u bytes, %d input fields%s\n", unsigned(descriptor.size()),
                      _descriptor.count, _descriptor.usesReportIds ? ", report IDs" : "");
        for (int i = 0; i < _descriptor.count; ++i) {
            const HidGamepad::Field& f = _descriptor.fields[i];
            Serial.printf("  id %u bit %u size %u page 0x%02X usage 0x%02X range %ld..%ld\n", f.reportId,
                          f.bitOffset, f.bitSize, f.usagePage, f.usage, long(f.logicalMin), long(f.logicalMax));
        }
    }
    slotCount = 0;
    std::vector<NimBLERemoteCharacteristic*>* characteristics = hid->getCharacteristics(true);
    int subscribed = 0;
    for (NimBLERemoteCharacteristic* c : *characteristics) {
        if (c->getUUID() != REPORT || !c->canNotify()) continue;
        uint8_t id = 0, kind = 1;
        NimBLERemoteDescriptor* reference = c->getDescriptor(REPORT_REFERENCE);
        if (reference) {
            const NimBLEAttValue value = reference->readValue();
            if (value.size() >= 2) { id = value.data()[0]; kind = value.data()[1]; }
        }
        if (kind != 1) continue;  // 1 = input report
        if (slotCount < 8) slots[slotCount++] = {c->getHandle(), id};
        if (c->subscribe(true, onNotify, true)) ++subscribed;
    }
    if (NimBLERemoteService* battery = client->getService(BATTERY_SERVICE)) {
        if (NimBLERemoteCharacteristic* level = battery->getCharacteristic(BATTERY_LEVEL)) {
            const NimBLEAttValue value = level->readValue();
            if (value.size()) _battery = value.data()[0];
        }
    }
    if (!subscribed) {
        Serial.println("Gamepad: no input reports to subscribe to");
        client->disconnect();
        _state = IDLE;
        return false;
    }
    _reportLogged = false;
    _state = CONNECTED;
    Serial.printf("Gamepad: ready, %d input report(s), battery %u%%\n", subscribed, unsigned(_battery.load()));
    return true;
}

void GamepadMgr::onReport(uint8_t reportId, const uint8_t* data, size_t length) {
    uint32_t reportRaw = 0;
    HidGamepad::Keys reportKeys;
    _padByReport[reportId] =
        HidGamepad::decode(_descriptor, reportId, data, length, HidGamepad::ANDROID_MAP, &reportRaw, &reportKeys);
    _rawByReport[reportId] = reportRaw;
    _otherByReport[reportId] = reportKeys.otherPad;
    // A report with keys (keyboard or media keys) replaces the keys it held
    // before; other reports (a touchpad) leave them alone.
    bool keyFields = false;
    for (int i = 0; i < _descriptor.count && !keyFields; ++i)
        keyFields = (_descriptor.fields[i].usagePage == HidGamepad::PAGE_KEYBOARD ||
                     _descriptor.fields[i].usagePage == HidGamepad::PAGE_CONSUMER) &&
                    (!_descriptor.usesReportIds || _descriptor.fields[i].reportId == reportId);
    if (keyFields) {
        _keysByReport[reportId & 3] = reportKeys;
        HidGamepad::Keys all;
        for (const HidGamepad::Keys& k : _keysByReport)
            for (int i = 0; i < k.count; ++i) all.add(k.codes[i]);
        portENTER_CRITICAL(&_keysLock);
        _keys = all;
        portEXIT_CRITICAL(&_keysLock);
    }
    // A pad may split its input over several reports (buttons, consumer keys):
    // combine them, each report contributing what it last said.
    uint16_t pad = 0, other = 0;
    uint32_t raw = 0;
    for (int i = 0; i < slotCount; ++i) {
        pad |= _padByReport[slots[i].id]; raw |= _rawByReport[slots[i].id]; other |= _otherByReport[slots[i].id];
    }
    if (!slotCount) { pad = _padByReport[reportId]; raw = reportRaw; other = reportKeys.otherPad; }
    _padOnly = other;
    const uint16_t previous = _buttons.load();
    const uint32_t previousRaw = _raw.load();
    _buttons = pad;
    _raw = raw;
    if (pad != previous || raw != previousRaw) _lastInputAt = millis();
    if (!_reportLogged || raw != previousRaw || pad != previous) {
        _reportLogged = true;
        char hex[3 * 16 + 1] = {};
        for (size_t i = 0; i < length && i < 16; ++i) snprintf(hex + 3 * i, 4, "%02X ", data[i]);
        Serial.printf("Gamepad: report %u [%s] buttons 0x%04lX -> pad 0x%04X\n", reportId, hex,
                      static_cast<unsigned long>(raw), pad);
    }
}

HidGamepad::Keys GamepadMgr::keys() {
    if (_state != CONNECTED) return HidGamepad::Keys();
    portENTER_CRITICAL(&_keysLock);
    const HidGamepad::Keys held = _keys;
    portEXIT_CRITICAL(&_keysLock);
    return held;
}

void GamepadMgr::onDisconnected() {
    _buttons = 0;
    _padOnly = 0;
    _raw = 0;
    memset(_padByReport, 0, sizeof(_padByReport));
    memset(_rawByReport, 0, sizeof(_rawByReport));
    memset(_otherByReport, 0, sizeof(_otherByReport));
    for (HidGamepad::Keys& k : _keysByReport) k = HidGamepad::Keys();
    portENTER_CRITICAL(&_keysLock);
    _keys = HidGamepad::Keys();
    portEXIT_CRITICAL(&_keysLock);
    if (_state == CONNECTED) Serial.println("Gamepad: disconnected");
    _state = _radioOn ? IDLE : OFF;
}

void GamepadMgr::taskEntry(void* self) {
    static_cast<GamepadMgr*>(self)->task();
}

void GamepadMgr::task() {
    uint32_t lastAttempt = 0;
    if (hasSaved()) _connectRequested = true;
    while (!_stopping) {
        if (_forgetRequested.exchange(false)) {
            const String address = savedAddress();
            Preferences prefs;
            prefs.begin(PREFS, false);
            prefs.clear();
            prefs.end();
            _saved = "";
            _savedLoaded = true;
            if (client && client->isConnected()) client->disconnect();
            if (_radioOn && address.length()) {
                NimBLEDevice::deleteBond(NimBLEAddress(std::string(address.c_str())));
                NimBLEDevice::deleteBond(NimBLEAddress(std::string(address.c_str()), 1));
            }
            _buttons = 0;
            Serial.println("Gamepad: forgotten");
        }
        if (_scanRequested.exchange(false) && startRadio()) {
            if (xSemaphoreTake(_lock, pdMS_TO_TICKS(200))) { _found.clear(); xSemaphoreGive(_lock); }
            const State before = _state;
            _state = SCANNING;
            NimBLEScan* scanner = NimBLEDevice::getScan();
            scanner->setAdvertisedDeviceCallbacks(&scanCallbacks, false);
            scanner->setActiveScan(true);
            scanner->setInterval(97);
            scanner->setWindow(67);
            Serial.printf("Gamepad: scanning for %lu s\n", static_cast<unsigned long>(_scanSeconds));
            scanner->start(_scanSeconds, false);  // blocking
            scanner->clearResults();
            _state = before == CONNECTED && client && client->isConnected() ? CONNECTED : (_radioOn ? IDLE : OFF);
            Serial.printf("Gamepad: scan done, %u device(s)\n", unsigned(found().size()));
        }
        const bool connected = client && client->isConnected() && _state == CONNECTED;
        if (!connected && hasSaved() &&
            (_connectRequested.exchange(false) || millis() - lastAttempt > RECONNECT_MS)) {
            lastAttempt = millis();
            connectSaved();
            lastAttempt = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (client && client->isConnected()) client->disconnect();
    if (_radioOn) {
        NimBLEDevice::deinit(true);
        client = nullptr;
        _radioOn = false;
    }
    _state = OFF;
    _task = nullptr;
    vTaskDelete(nullptr);
}

void GamepadMgr::pollActions(const std::function<void(int)>& action) {
    const uint16_t now = buttons();
    using namespace HidGamepad;
    if (_rawMode) {
        _lastButtons = now;
        _repeatButton = 0;
        return;
    }
    const uint16_t pressed = now & ~_lastButtons;
    _lastButtons = now;
    auto emit = [&](uint16_t button) {
        if (button & (PAD_DOWN | PAD_RIGHT)) action(1);
        else if (button & (PAD_UP | PAD_LEFT)) action(2);
        else if (button & (PAD_A | PAD_START | PAD_X)) action(3);
        else if (button & (PAD_B | PAD_SELECT | PAD_Y)) action(4);
        else if (button & PAD_HOME) action(5);
    };
    constexpr uint16_t DPAD = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    if (pressed) {
        // One action per new press, highest priority first.
        for (uint16_t bit = 1; bit; bit <<= 1)
            if (pressed & bit) { emit(bit); break; }
        _repeatButton = pressed & DPAD;
        _repeatAt = millis() + REPEAT_DELAY_MS;
    } else if (_repeatButton && (now & _repeatButton)) {
        // Held d-pad auto-repeats in lists.
        if (long(millis() - _repeatAt) >= 0) {
            emit(_repeatButton);
            _repeatAt = millis() + REPEAT_MS;
        }
    } else {
        _repeatButton = 0;
    }
}

bool GamepadMgr::serialCommand(const char* line) {
    if (strncmp(line, "bt", 2) != 0) return false;
    char address[24] = {};
    int type = 0;
    if (!strcmp(line, "bt scan")) {
        scan(10);
    } else if (sscanf(line, "bt pair %23s %d", address, &type) >= 1) {
        String name;
        for (const Found& f : found()) if (f.address == address) { name = f.name; type = f.addressType; }
        pair(address, type, name);
    } else if (!strcmp(line, "bt forget")) {
        forget();
    } else {
        Serial.printf("Gamepad: %s, saved \"%s\" %s, buttons 0x%04X raw 0x%04lX, battery %u%%\n", stateName(),
                      savedName().c_str(), savedAddress().c_str(), buttons(),
                      static_cast<unsigned long>(rawButtons()), unsigned(battery()));
    }
    return true;
}
