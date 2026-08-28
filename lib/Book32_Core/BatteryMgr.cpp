#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"
#include <esp_sleep.h>
#include "Book32FS.h"
#include "DisplayMgr.h"
#include <ArduinoJson.h>
#include <Fonts/FreeSans18pt7b.h>
#include <esp32-hal-cpu.h>

// Static constants
const float BatteryMgr::CHARGE_THRESHOLD = 0.03f;  // 30mV increase = charging (avoid false positives from fluctuation)
const float BatteryMgr::CRITICAL_VOLTAGE = 3.0f;   // Shutdown at 3.0V
const float BatteryMgr::HIGH_VOLTAGE_THRESHOLD = 4.0f;  // Assume charging if voltage >= this
const float BatteryMgr::SPIKE_REJECT_THRESHOLD = 0.5f;  // Reject readings that jump > 0.5V

static int voltageToPercentage(float voltage) {
    if (voltage >= BATTERY_FULL_VOLTAGE) return 100;
    if (voltage <= BATTERY_EMPTY_VOLTAGE) return 0;

    return (int)(((voltage - BATTERY_EMPTY_VOLTAGE) /
                 (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) * 100.0f + 0.5f);
}

BatteryMgr::BatteryMgr() : _lastReadTime(0), _historyIndex(0), _lastHistoryUpdate(0),
                           _previousVoltage(0.0f), _lastValidVoltage(0.0f),
                           _criticalCount(0), _lastChargingTime(0), _sleepTimeoutMinutes(0),
                           _sleepMessage("Press button to wake"), _lastActivityTime(0),
                           _readerActive(false), _cpuReduced(false),
                           _lastDisplayedCharging(false), _lastIndicatorUpdate(0) {
    _cachedStatus = {0.0f, 0, false};
    // Initialize history
    for (int i = 0; i < 5; i++) {
        _voltageHistory[i] = 0.0f;
        _historyTimes[i] = 0;
    }
}

BatteryMgr& BatteryMgr::getInstance() {
    static BatteryMgr instance;
    return instance;
}

void BatteryMgr::init() {
    pinMode(PIN_BAT_VOLT, INPUT);
#ifdef PIN_VBAT_SWITCH
    pinMode(PIN_VBAT_SWITCH, OUTPUT);
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL); // Keep it off
#endif
    // ADC calibration/attentuation might be needed for S3
    analogSetAttenuation(ADC_11db);

    // Perform initial read to populate cache
    updateCache();

    // Initialize voltage history and previous voltage with current reading
    _previousVoltage = _cachedStatus.voltage;
    for (int i = 0; i < 5; i++) {
        _voltageHistory[i] = _cachedStatus.voltage;
        _historyTimes[i] = millis();
    }
    _lastHistoryUpdate = millis();

    Serial.printf("Battery: Initial voltage %.2fV (%d%%)\n", _cachedStatus.voltage, _cachedStatus.percentage);

    // Load sleep settings from EbookFS
    loadSleepSettings();

    // Initialize activity timer
    _lastActivityTime = millis();
}

void BatteryMgr::update() {
    unsigned long now = millis();

    if (_readerActive && !_cpuReduced && now - _lastActivityTime >= 1500) {
        if (setCpuFrequencyMhz(80)) {
            _cpuReduced = true;
            Serial.println("AppReader: CPU idling at 80 MHz");
        }
    }

    // Update voltage history periodically for trend analysis
    if (now - _lastHistoryUpdate >= HISTORY_INTERVAL_MS) {
        // Make sure cache is fresh
        if (now - _lastReadTime >= CACHE_DURATION_MS) {
            updateCache();
        }

        // Add current voltage to history
        _voltageHistory[_historyIndex] = _cachedStatus.voltage;
        _historyTimes[_historyIndex] = now;
        _historyIndex = (_historyIndex + 1) % 5;
        _lastHistoryUpdate = now;

        // Check if voltage is trending upward (charging)
        // Compare oldest reading to current
        int oldestIndex = _historyIndex;  // After increment, this points to oldest
        float oldestVoltage = _voltageHistory[oldestIndex];
        unsigned long oldestTime = _historyTimes[oldestIndex];

        // Compare with oldest reading (at least 90 seconds old for slow charging detection)
        if (oldestTime > 0 && (now - oldestTime) >= 90000) {
            float voltageChange = _cachedStatus.voltage - oldestVoltage;

            // If voltage increased by more than threshold, we're charging
            if (voltageChange > CHARGE_THRESHOLD) {
                if (!_cachedStatus.charging) {
                    _cachedStatus.charging = true;
                    Serial.printf("Battery: Charging detected via trend (%.3fV -> %.3fV, +%.3fV)\n",
                                 oldestVoltage, _cachedStatus.voltage, voltageChange);
                }
                _lastChargingTime = now;  // Track when charging was last seen
            } else if (_cachedStatus.voltage < HIGH_VOLTAGE_THRESHOLD && voltageChange < -CHARGE_THRESHOLD) {
                // Voltage is dropping and below high threshold = not charging
                if (_cachedStatus.charging) {
                    _cachedStatus.charging = false;
                    Serial.printf("Battery: Discharging detected (%.3fV -> %.3fV, %.3fV)\n",
                                 oldestVoltage, _cachedStatus.voltage, voltageChange);
                }
            }
        }
    }

    // Check for critical low battery
    if (isCriticallyLow()) {
        Serial.println("CRITICAL: Battery voltage too low! Shutting down...");
        shutdownLowBattery();
    }

    // Check for idle timeout (only if enabled and not charging)
    if (_sleepTimeoutMinutes > 0 && !_cachedStatus.charging) {
        unsigned long idleTime = now - _lastActivityTime;
        unsigned long timeoutMs = (unsigned long)_sleepTimeoutMinutes * 60 * 1000;
        if (idleTime >= timeoutMs) {
            Serial.printf("Idle timeout reached (%d minutes). Entering sleep...\n", _sleepTimeoutMinutes);
            enterIdleSleep();
        }
    }
}

void BatteryMgr::updateCache(bool clearStaleCharging) {
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, VBAT_SWITCH_LEVEL); // Turn on measurement
    delay(5); // Wait for stabilization
#endif

    // Read ADC - average 30 samples for stability
    uint32_t raw = 0;
    for(int i = 0; i < 30; i++) {
        raw += analogRead(PIN_BAT_VOLT);
        delay(1);
    }
    raw /= 30;

#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL); // Turn off to save power
#endif

    // Convert to battery voltage through the 2:1 divider, then apply the board
    // calibration factor from Config.h.
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
    voltage *= BATTERY_VOLTAGE_CALIBRATION;
    if (voltage > BATTERY_FULL_VOLTAGE) {
        voltage = BATTERY_FULL_VOLTAGE;
    }

    // Spike rejection: discard readings that jump too far from last valid reading
    // This protects against ADC noise during heavy WiFi activity
    if (_lastValidVoltage > 0.0f && fabsf(voltage - _lastValidVoltage) > SPIKE_REJECT_THRESHOLD) {
        Serial.printf("Battery: SPIKE REJECTED (%.3fV -> %.3fV, delta=%.3fV) - keeping %.3fV\n",
                     _lastValidVoltage, voltage, voltage - _lastValidVoltage, _lastValidVoltage);
        voltage = _lastValidVoltage;  // Keep previous valid reading
    } else {
        _lastValidVoltage = voltage;  // Accept as valid
    }

    // Calculate percentage (LiPo: 3.0V = 0%, 4.2V = 100%)
    int percentage = voltageToPercentage(voltage);

    float previousVoltage = _previousVoltage;

    // Preserve charging state from trend analysis unless this is an explicit
    // UI refresh, where a stale "charging" label is worse than showing full.
    bool currentCharging = _cachedStatus.charging;
    if (clearStaleCharging) {
        currentCharging = false;
    }

    // Quick charging detection: if voltage increased since last read, we're likely charging
    if (previousVoltage > 0 && voltage > previousVoltage + 0.02f) {
        // Voltage increased by >20mV since last read - likely charging
        if (!currentCharging) {
            currentCharging = true;
            Serial.printf("Battery: Quick charge detect (%.3fV -> %.3fV, +%.3fV)\n",
                         previousVoltage, voltage, voltage - previousVoltage);
        }
        _lastChargingTime = millis();
    } else if (previousVoltage > 0 && voltage < previousVoltage - 0.01f) {
        currentCharging = false;
    }

    // High voltage means "full", not necessarily connected to the charger. We
    // only label it charging when voltage is actually rising.

    // Update previous voltage for next comparison
    _previousVoltage = voltage;

    // Update cache
    _cachedStatus = {voltage, percentage, currentCharging};
    _lastReadTime = millis();
}

bool BatteryMgr::isCriticallyLow() {
    // Make sure we have a fresh reading
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }

    // Charging grace period: if charging was detected recently, don't allow critical shutdown
    // WiFi noise can simultaneously corrupt voltage AND flip charging state
    if (_lastChargingTime > 0 && (millis() - _lastChargingTime) < CHARGING_GRACE_MS) {
        _criticalCount = 0;
        return false;
    }

    // Require consecutive critical readings to prevent single-spike shutdown
    if (_cachedStatus.voltage <= CRITICAL_VOLTAGE && !_cachedStatus.charging) {
        _criticalCount++;
        Serial.printf("Battery: Critical reading #%d (%.2fV)\n", _criticalCount, _cachedStatus.voltage);
        if (_criticalCount >= CRITICAL_CONFIRM_COUNT) {
            return true;  // Confirmed critically low
        }
    } else {
        if (_criticalCount > 0) {
            Serial.printf("Battery: Critical counter reset (voltage=%.2fV, charging=%s)\n",
                         _cachedStatus.voltage, _cachedStatus.charging ? "yes" : "no");
        }
        _criticalCount = 0;  // Reset counter on any normal reading
    }
    return false;
}

void BatteryMgr::shutdownLowBattery() {
    Serial.println("Battery critically low - entering deep sleep");
    Serial.printf("Voltage: %.2fV\n", _cachedStatus.voltage);
    Serial.flush();

    // Small delay to let serial finish
    delay(100);

    // Enter deep sleep indefinitely (will wake on reset/power)
    // This is the safest way to "power off" on ESP32
    esp_deep_sleep_start();
}

BatteryStatus BatteryMgr::getStatus() {
    // Refresh cache if expired
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }
    return _cachedStatus;
}

BatteryStatus BatteryMgr::refreshNow() {
    updateCache(true);
    return _cachedStatus;
}

float BatteryMgr::getVoltage() {
    return getStatus().voltage;
}

int BatteryMgr::getPercentage() {
    return getStatus().percentage;
}

bool BatteryMgr::isCharging() {
    return getStatus().charging;
}

void BatteryMgr::loadSleepSettings() {
    // Load from EbookFS partition
    if (EbookFS.exists("/sleep_config.json")) {
        File file = EbookFS.open("/sleep_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, file)) {
                _sleepTimeoutMinutes = doc.containsKey("sleepTimeout") ? doc["sleepTimeout"].as<int>() : 0;
                _sleepMessage = doc["sleepMessage"] | "Press button to wake";
                Serial.printf("Loaded sleep settings: timeout=%d min, message=%s\n",
                             _sleepTimeoutMinutes, _sleepMessage.c_str());
            }
            file.close();
        }
    } else {
        // Use defaults (sleep disabled)
        _sleepTimeoutMinutes = 0;
        _sleepMessage = "Press button to wake";
        Serial.println("Using default sleep settings (sleep disabled)");
    }
}

void BatteryMgr::resetIdleTimer() {
    _lastActivityTime = millis();
    if (_cpuReduced) {
        setCpuFrequencyMhz(240);
        _cpuReduced = false;
    }
}

void BatteryMgr::setReaderActive(bool active) {
    if (!active && _cpuReduced) {
        setCpuFrequencyMhz(240);
        _cpuReduced = false;
    }
    _readerActive = active;
    _lastActivityTime = millis();
}

void BatteryMgr::enterIdleSleep() {
    Serial.println("Entering idle sleep...");
    Serial.printf("Sleep message: %s\n", _sleepMessage.c_str());
    Serial.flush();

    // Display sleep message on e-ink
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSans18pt7b);
        display.setTextColor(GxEPD_BLACK);

        // Calculate text bounds for centering
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(_sleepMessage.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);

        // Center the text on screen
        int16_t x = (display.width() - tbw) / 2 - tbx;
        int16_t y = (display.height() - tbh) / 2 - tby;

        display.setCursor(x, y);
        display.print(_sleepMessage);
    } while (display.nextPage());

    // Wait for display to finish updating
    delay(100);

    // Configure wake sources
    // Wake on button press (GPIO5 on TRMNL, active LOW)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);  // 0 = wake on LOW

    // Enter deep sleep
    Serial.println("Going to deep sleep...");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

void BatteryMgr::drawStatusIndicator() {
    // Only update if charging state actually changed
    bool currentCharging = _cachedStatus.charging;

    // Only refresh display when charging state changes (plugged in or unplugged)
    if (currentCharging == _lastDisplayedCharging) {
        return;  // No change, no update needed
    }

    // Get display reference
    Book32Display& display = DisplayMgr::getInstance().getDisplay();

    // Indicator position (top-right corner)
    // Small 50x25 area for a battery icon with charging indicator
    const int INDICATOR_WIDTH = 55;
    const int INDICATOR_HEIGHT = 30;
    const int INDICATOR_X = display.width() - INDICATOR_WIDTH - 5;
    const int INDICATOR_Y = 5;

    // Use partial window for just the indicator area
    display.setPartialWindow(INDICATOR_X, INDICATOR_Y, INDICATOR_WIDTH, INDICATOR_HEIGHT);

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Battery outline
        int batX = INDICATOR_X + 5;
        int batY = INDICATOR_Y + 5;
        int batW = 40;
        int batH = 20;

        display.drawRect(batX, batY, batW, batH, GxEPD_BLACK);
        display.fillRect(batX + batW, batY + 5, 3, 10, GxEPD_BLACK);  // Battery tip

        // Battery fill based on percentage
        int fillWidth = (_cachedStatus.percentage * (batW - 4)) / 100;
        if (fillWidth > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, batH - 4, GxEPD_BLACK);
        }

        // Draw lightning bolt if charging
        if (currentCharging) {
            // Draw white lightning bolt on the black fill
            int boltX = batX + batW / 2;
            int boltY = batY + 2;
            // Simple lightning bolt shape
            display.drawLine(boltX, boltY, boltX - 4, batY + batH/2, GxEPD_WHITE);
            display.drawLine(boltX - 4, batY + batH/2, boltX + 2, batY + batH/2, GxEPD_WHITE);
            display.drawLine(boltX + 2, batY + batH/2, boltX - 2, batY + batH - 2, GxEPD_WHITE);
        }

    } while (display.nextPage());

    // Update tracking
    _lastDisplayedCharging = currentCharging;

    Serial.printf("Battery indicator updated: %s\n", currentCharging ? "Charging" : "Not charging");
}
