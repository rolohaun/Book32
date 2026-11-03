#include "AppSelection.h"
#include "Config.h"
#include <Preferences.h>
#include <esp_partition.h>

namespace {
constexpr const char* PREFS = "inkdeck";
constexpr const char* KEY_APPS = "apps";
constexpr const char* KEY_INSTALL = "installId";
constexpr size_t RECORD_SECTOR = 4096;
}

AppSelection& AppSelection::getInstance() {
    static AppSelection instance;
    return instance;
}

uint8_t AppSelection::available() {
    return TODO | KLIPPER | (BOOK32_HAS_INKBOY ? INKADE : 0);
}

void AppSelection::begin() {
    Preferences prefs;
    if (!prefs.begin(PREFS, false)) return;  // keeps ALL
    const esp_partition_t* app0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    InstallRecord record;
    if (app0 && app0->size > RECORD_SECTOR &&
        esp_partition_read(app0, app0->size - RECORD_SECTOR, &record, sizeof(record)) == ESP_OK &&
        !memcmp(record.magic, "INKAPPS1", 8) && record.version == 1 &&
        record.crc == installCrc32(reinterpret_cast<const uint8_t*>(&record), offsetof(InstallRecord, crc)) &&
        record.installId != prefs.getULong(KEY_INSTALL, 0)) {
        prefs.putUChar(KEY_APPS, record.apps & ALL);
        prefs.putULong(KEY_INSTALL, record.installId);
        Serial.printf("Apps: installer chose 0x%02X (install %08lx)\n", record.apps & ALL,
                      static_cast<unsigned long>(record.installId));
    }
    _mask = prefs.getUChar(KEY_APPS, ALL) & ALL;
    prefs.end();
    Serial.printf("Apps: 0x%02X (%s)\n", _mask, launcher() ? "main menu" : "library home");
}

bool AppSelection::save(uint8_t mask) {
    Preferences prefs;
    if (!prefs.begin(PREFS, false)) return false;
    const bool ok = prefs.putUChar(KEY_APPS, mask & ALL) == 1;
    prefs.end();
    return ok;
}
