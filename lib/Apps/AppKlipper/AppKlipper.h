#ifndef APP_KLIPPER_H
#define APP_KLIPPER_H

#include "BaseApp.h"
#include "../../Book32_Core/InputMgr.h"
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

enum KlipperState {
    KLIPPER_SCANNING,
    KLIPPER_LIST,
    KLIPPER_DETAILS
};

struct PrinterInfo {
    String hostname;
    String ip;
    uint16_t port;
    // Status data
    String state;           // "ready", "printing", "paused", "error"
    float extruderTemp;
    float extruderTarget;
    float bedTemp;
    float bedTarget;
    float progress;         // 0-100%
    String filename;        // Current print file
};

class AppKlipper : public App {
public:
    AppKlipper();
    virtual ~AppKlipper();

    // App Interface
    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    // Icon
    const uint8_t* getIconImage() override;
    const char* getName() override { return "Klipper"; }

    void handleInput(InputAction action);

    // Static task function for FreeRTOS
    static void scanTaskWrapper(void* param);
    void scanTask();  // The actual scanning logic

private:
    KlipperState _state;
    std::vector<PrinterInfo> _printers;
    int _selectedIndex;
    bool _needsRedraw;
    volatile bool _scanning;  // volatile for thread safety
    volatile bool _scanComplete;  // Flag to indicate scan finished
    unsigned long _lastScanTime;
    unsigned long _lastUpdateTime;
    TaskHandle_t _scanTaskHandle;  // FreeRTOS task handle
    TaskHandle_t _asyncTcpTask;    // async_tcp task handle for WDT management

    // Scan progress
    volatile int _scanProgress;  // 0-100%
    volatile int _scannedIPs;
    int _totalIPs;
    int _lastDisplayedScanChunk;

    // Found printers during scan (temporary storage)
    std::vector<PrinterInfo> _foundPrinters;

    // Full refresh settings (0 = partial only, >0 = minutes between full refreshes)
    int _fullRefreshInterval;  // Minutes between full refreshes (default 5)
    unsigned long _lastFullRefreshTime;
    bool _firstDraw;
    unsigned long _statusUpdateInterval;  // Seconds between status polls (default 30)

    // Settings
    void loadSettings();

    // Scanning
    void startScan();  // Starts the background scan task
    bool probeMoonraker(const String& ip, uint16_t port);  // Check if IP is Moonraker
    void drawScanning();

    // List view
    void drawPrinterList();

    // Details view
    void drawPrinterDetails();
    void fetchPrinterStatus(PrinterInfo& printer);

    // Helper to make HTTP request to Moonraker API
    String httpGet(const String& url);
};

#endif
