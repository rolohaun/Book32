#pragma once
#include <Arduino.h>

// Which optional apps this InkDeck runs. The eReader (and Settings on touch
// boards) are always there; with no optional app the library is the home
// screen, otherwise the main menu is.
//
// The web installer chooses the apps: it writes an InstallRecord into the
// last 4 KB sector of the app0 partition, which the firmware image never
// reaches. On boot a record with a new install ID is copied to NVS, where
// the web page can change the choice later. Devices that never had a record
// (updated over the air from older versions) keep every app.
class AppSelection {
public:
    enum App : uint8_t { TODO = 0x01, KLIPPER = 0x02, INKADE = 0x04 };
    static constexpr uint8_t ALL = TODO | KLIPPER | INKADE;

    static AppSelection& getInstance();

    // Reads NVS and any new installer record. Call once before registering apps.
    void begin();
    bool enabled(App app) const { return _mask & app; }
    uint8_t mask() const { return _mask; }
    // Apps this board has at all.
    static uint8_t available();
    // The main menu is shown only when an optional app is enabled.
    bool launcher() const { return _mask & available(); }
    // Saved for the next restart; apps are registered at boot.
    bool save(uint8_t mask);

private:
    AppSelection() = default;
    uint8_t _mask = ALL;
};

// The installer's record, little-endian, at the start of app0's last sector.
struct __attribute__((packed)) InstallRecord {
    char magic[8];       // "INKAPPS1"
    uint8_t version;     // 1
    uint8_t apps;        // AppSelection::App bits
    uint16_t reserved;   // 0
    uint32_t installId;  // random per flash: applied once
    uint32_t crc;        // CRC-32 (zlib) of the 16 bytes above
};
static_assert(sizeof(InstallRecord) == 20, "installer record layout");

// CRC-32 as zlib and JavaScript implementations compute it.
inline uint32_t installCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
    return ~crc;
}
