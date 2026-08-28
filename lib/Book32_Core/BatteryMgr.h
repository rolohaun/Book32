#pragma once
#include <Arduino.h>

// Combined battery status to avoid multiple ADC reads
struct BatteryStatus {
    float voltage;
    int percentage;
    bool charging;
};

class BatteryMgr {
public:
    static BatteryMgr& getInstance();

    void init();

    // Call periodically from main loop to update charge detection and check critical battery
    void update();

    // Preferred: Get all battery info from single cached read
    BatteryStatus getStatus();
    BatteryStatus refreshNow();

    // Legacy methods (still work, but use getStatus() to avoid multiple reads)
    float getVoltage();
    int getPercentage();
    bool isCharging();

    // Check if battery is critically low (should shutdown)
    bool isCriticallyLow();

    // Safely power off the device (deep sleep)
    void shutdownLowBattery();

    // Idle sleep management
    void loadSleepSettings();             // Load from EbookFS
    void resetIdleTimer();                // Call when user interacts
    void enterIdleSleep();                // Display message and sleep
    void setReaderActive(bool active);    // Enables reader-only idle CPU scaling

    // Status indicator on e-ink display (partial update)
    void drawStatusIndicator();           // Draw charging indicator if state changed

private:
    BatteryMgr();

    // Internal: performs actual ADC read and updates cache
    void updateCache(bool clearStaleCharging = false);

    // Cached values
    BatteryStatus _cachedStatus;
    unsigned long _lastReadTime;
    static const unsigned long CACHE_DURATION_MS = 5000; // Cache for 5 seconds

    // Charge detection via voltage trend
    float _voltageHistory[5];          // Store 5 readings over time (for 2+ minute trend)
    unsigned long _historyTimes[5];    // Timestamps for each reading
    int _historyIndex;                 // Current index in circular buffer
    unsigned long _lastHistoryUpdate;  // Last time we added to history
    float _previousVoltage;            // Previous voltage reading for short-term trend
    static const unsigned long HISTORY_INTERVAL_MS = 30000; // Sample every 30 seconds (better for slow charging)
    static const float CHARGE_THRESHOLD;  // Voltage increase threshold to detect charging
    static const float HIGH_VOLTAGE_THRESHOLD;  // Consider charging if voltage above this (near full)

    // Critical battery threshold
    static const float CRITICAL_VOLTAGE;  // Shutdown below this voltage

    // Spike rejection and false-shutdown protection
    float _lastValidVoltage;              // Last plausible voltage reading
    int _criticalCount;                   // Consecutive critical readings counter
    unsigned long _lastChargingTime;      // Last time charging was detected
    static const int CRITICAL_CONFIRM_COUNT = 3;     // Require N consecutive critical readings
    static const float SPIKE_REJECT_THRESHOLD;       // Max plausible V change per read (0.5V)
    static const unsigned long CHARGING_GRACE_MS = 60000;  // 60s grace after charging detected

    // Idle sleep settings
    int _sleepTimeoutMinutes;          // 0 = disabled
    String _sleepMessage;
    unsigned long _lastActivityTime;   // Last user interaction
    bool _readerActive;
    bool _cpuReduced;

    // Status indicator tracking
    bool _lastDisplayedCharging;       // Last charging state shown on display
    unsigned long _lastIndicatorUpdate;  // Last time indicator was updated

    // T-Energy-S3 typical divider: Voltage * 2
    // Some boards use different dividers. We'll start with x2.
    // 18650 Range: 3.0V (0%) to 4.2V (100%)
};
