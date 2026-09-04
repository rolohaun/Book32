#include "SoundMgr.h"

#include "Book32FS.h"
#include "Config.h"
#include <ArduinoJson.h>

namespace {
constexpr uint8_t BUZZER_CHANNEL = 7;
constexpr uint16_t BEEP_FREQUENCY_HZ = 2400;
constexpr uint16_t BEEP_DURATION_MS = 35;
constexpr const char* SOUND_CONFIG_PATH = "/sound_config.json";
}

SoundMgr& SoundMgr::getInstance() {
    static SoundMgr instance;
    return instance;
}

bool SoundMgr::isSupported() const {
    return BOOK32_HAS_BUZZER != 0;
}

void SoundMgr::init() {
#if BOOK32_HAS_BUZZER
    _enabled = true;
    if (EbookFS.exists(SOUND_CONFIG_PATH)) {
        File file = EbookFS.open(SOUND_CONFIG_PATH, "r");
        if (file) {
            DynamicJsonDocument doc(128);
            if (deserializeJson(doc, file) == DeserializationError::Ok) {
                _enabled = doc["enabled"] | true;
            }
            file.close();
        }
    }

    ledcSetup(BUZZER_CHANNEL, BEEP_FREQUENCY_HZ, 10);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
    ledcWrite(BUZZER_CHANNEL, 0);
#else
    _enabled = false;
#endif
    _initialized = true;
}

void SoundMgr::beep() {
#if BOOK32_HAS_BUZZER
    if (!_initialized || !_enabled) return;
    ledcWriteTone(BUZZER_CHANNEL, BEEP_FREQUENCY_HZ);
    delay(BEEP_DURATION_MS);
    ledcWriteTone(BUZZER_CHANNEL, 0);
#endif
}

void SoundMgr::setEnabled(bool enabled, bool persist) {
    _enabled = isSupported() && enabled;
    if (persist) save();
}

void SoundMgr::save() {
#if BOOK32_HAS_BUZZER
    DynamicJsonDocument doc(128);
    doc["enabled"] = _enabled;
    File file = EbookFS.open(SOUND_CONFIG_PATH, FILE_WRITE);
    if (!file) return;
    serializeJson(doc, file);
    file.close();
#endif
}
