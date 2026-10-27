#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include "Config.h"

// Define the display class here to be used across the app
#if defined(BOARD_LILYGO_T5S3_PRO)
#include "LilygoDisplay.h"
typedef LilygoDisplay Book32Display;
#elif defined(BOARD_SEEED_STICKY)
#include "StickyDisplay.h"
typedef StickyDisplay Book32Display;
#elif defined(BOARD_WAVESHARE_RLCD42)
#include "RlcdDisplay.h"
typedef RlcdDisplay Book32Display;
#else
// GxEPD2_750_T7 with a shorter partial-update waveform (~0.9s instead of 1.6s),
// plus Inkade's fast game video.
#include "Book32Display.h"
// The 7.5" panel blocks for ~0.9s per partial and ~1.6s per full refresh, so
// boot-screen frames are drawn on a worker task while startup continues.
#define BOOK32_ASYNC_BOOT_SCREEN 1
#endif

#ifndef BOOK32_ASYNC_BOOT_SCREEN
#define BOOK32_ASYNC_BOOT_SCREEN 0
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
    // Waits until the latest boot-screen status is on the panel and hands the
    // display back to the caller. Call before the first app draw.
    void finishBootScreen();

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

    void renderBootScreen(uint8_t progress, const char* status);
#if BOOK32_ASYNC_BOOT_SCREEN
    // Only the newest requested status is kept; statuses superseded while the
    // panel is busy are skipped instead of making startup wait for them.
    static void bootScreenTask(void* parameter);
    void waitForBootScreenIdle();
    SemaphoreHandle_t _bootLock = nullptr;
    volatile bool _bootTaskRunning = false;
    bool _bootPending = false;
    bool _bootFinished = false;
    uint8_t _bootProgress = 0;
    char _bootStatus[40] = "";
#endif
};
