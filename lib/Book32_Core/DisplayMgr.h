#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include "Config.h"

// Define the display class here to be used across the app
#if defined(BOARD_SEEED_STICKY)
#include "StickyDisplay.h"
typedef StickyDisplay Book32Display;
#else
typedef GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> Book32Display;
#endif

class DisplayMgr {
public:
    static DisplayMgr& getInstance();
    
    void init();
    void update(); // Handles partial updates if needed

    Book32Display& getDisplay() { return display; }

    void clear();
    void fullRefresh();
    void showBootScreen(uint8_t progress, const char* status);

    // Display orientation. Only the two portrait orientations are supported so
    // that every screen layout (480x800) stays valid: 3 = buttons on the right
    // (default), 1 = rotated 180 (buttons on the left). Applies OS-wide.
    void setRotation(int rotation);
    int getRotation() const { return _rotation; }
    bool mapNativeTouchToScreen(uint16_t nativeX, uint16_t nativeY,
                                uint16_t& screenX, uint16_t& screenY) const;
    void loadDisplaySettings();  // Reads /display_config.json (call after FS mount)

private:
    DisplayMgr();
    Book32Display display;
    bool _bootScreenActive = false;
    int _rotation = 3;  // Default: portrait, buttons on the right
};
