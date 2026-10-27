#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// What the home screen does in the background, whichever screen is home: the
// main menu, or the eReader's library when no optional app is installed.
// While home shows, Wi-Fi is woken (the web page uploads books), the Book32
// falls back to its setup hotspot when offline, and updates are checked.
class HomeServices {
public:
    static HomeServices& getInstance();

    // A home screen appeared / went away (leaving stops the hotspot).
    void enter();
    void leave();
    // Every loop while home shows. True when footerText() or the update
    // notice changed and the screen should repaint them.
    bool poll();

    String footerText() const;
    bool updateAvailable() const { return _updateAvailable; }
    String updateVersion() const { return _updateVersion; }

    // The library tells whether it is the home screen right now (library
    // shown, no book open, no main menu installed).
    void setLibraryHome(bool home) { _libraryHome = home; }
    // A home screen is the current app's screen.
    bool homeShowing() const;
    // The eReader (reading) or Inkade is open: they keep the radio off.
    bool offlineAppActive() const;

    void startHotspot();
    void stopHotspot();

private:
    HomeServices() = default;
    static void updateCheckTask(void* parameter);
    static void wifiWakeTask(void* parameter);
    static void wifiWakeWork(void* parameter);
    void ensureWifiAwake();

    std::atomic<bool> _libraryHome{false};
    std::atomic<bool> _wifiStarting{false}, _hotspotActive{false};
    std::atomic<bool> _changed{false};
    bool _updateAvailable = false;
    String _updateVersion;
    TaskHandle_t _updateTaskHandle = nullptr;
    TaskHandle_t _wifiTaskHandle = nullptr;
    unsigned long _lastNetworkPoll = 0;
    String _lastFooter;
};
