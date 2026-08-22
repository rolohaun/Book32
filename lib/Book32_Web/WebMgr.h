#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class AsyncWebServer; // Forward declaration
class AsyncWebServerRequest; // Forward declaration

class WebMgr {
public:
    static WebMgr& getInstance();
    
    void mountFilesystems();  // Call early, before WiFi
    void init();              // Call after WiFi connected
    void stop();              // Stop web services before powering WiFi down
    void update(); // Handle any main loop needs (including pending OTA)
    bool isInitialized() const { return _initialized; }
    
    volatile bool _otaPending = false;  // Flag to trigger OTA from main loop

    // Deferred display changes: set by async web handlers, applied from the main
    // loop (in update()) so drawing never happens on the async server task.
    volatile int _pendingRotation = 0;        // 0 = none, else 1 or 3
    volatile int _pendingReaderFontSize = 0;  // 0 = none, else 9/12/18
    volatile int _pendingReaderFontFamily = -1; // -1 = none, 0 = native, 1 = Open Sans

private:
    WebMgr();
    AsyncWebServer* server; // Pointer instead of object
    bool _initialized = false;
    bool _endpointsConfigured = false;
    
    void setupEndpoints();
    // Handlers need full type in cpp, so keeping signature here implies we need *AsyncWebServerRequest in cpp
    void handleAPIStatus(AsyncWebServerRequest *request);
    void handleAPISettingsGet(AsyncWebServerRequest *request);
    void handleAPISettingsSet(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleAPICheckUpdate(AsyncWebServerRequest *request);
    void handleAPIUpdateTrigger(AsyncWebServerRequest *request);
};
