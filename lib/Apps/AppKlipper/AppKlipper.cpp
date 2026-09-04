#include "AppKlipper.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "FontMgr.h"
#include "Book32FS.h"
#include "icon_klipper.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

// Scan interval (30 seconds)
static const unsigned long SCAN_INTERVAL = 30000;
// Default status update interval (30 seconds)

AppKlipper::AppKlipper() {
    _state = KLIPPER_SCANNING;
    _selectedIndex = 0;
    _needsRedraw = true;
    _scanning = false;
    _scanComplete = false;
    _lastScanTime = 0;
    _lastUpdateTime = 0;
    _scanProgress = 0;
    _scannedIPs = 0;
    _totalIPs = 0;
    _lastDisplayedScanChunk = 0;
    _scanTaskHandle = NULL;
    _fullRefreshInterval = 5;  // Default: full refresh every 5 minutes
    _lastFullRefreshTime = 0;
    _firstDraw = true;
    _statusUpdateInterval = 30;  // Default: 30 seconds
    loadSettings();
}

void AppKlipper::loadSettings() {
    // Load settings from EbookFS (persists through OTA updates)
    if (EbookFS.exists("/klipper_config.json")) {
        File file = EbookFS.open("/klipper_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(256);
            if (!deserializeJson(doc, file)) {
                _fullRefreshInterval = doc["fullRefreshInterval"] | 5;
                _statusUpdateInterval = doc["statusUpdateInterval"] | 30;
                Serial.printf("Klipper: Loaded settings - fullRefreshInterval=%d min, statusUpdate=%lu sec\n",
                             _fullRefreshInterval, _statusUpdateInterval);
            }
            file.close();
        }
    }
}

AppKlipper::~AppKlipper() {
    // Stop scan task if running
    if (_scanTaskHandle != NULL) {
        vTaskDelete(_scanTaskHandle);
        _scanTaskHandle = NULL;
    }
    _printers.clear();
    _foundPrinters.clear();
}

void AppKlipper::start() {
    Serial.println("Klipper App Started");
    _state = KLIPPER_SCANNING;
    _selectedIndex = 0;
    _needsRedraw = true;
    _scanning = false;
    _scanComplete = false;
    _lastScanTime = 0;
    _lastUpdateTime = 0;
    _lastFullRefreshTime = 0;
    _firstDraw = true;
    _scanTaskHandle = NULL;

    // Unsubscribe async_tcp from WDT for the entire Klipper session.
    // All HTTP requests (scan + status fetch) cause lwIP contention
    // that prevents async_tcp from feeding its watchdog.
    _asyncTcpTask = xTaskGetHandle("async_tcp");
    if (_asyncTcpTask != NULL) {
        esp_task_wdt_delete(_asyncTcpTask);
        Serial.println("async_tcp unsubscribed from WDT (Klipper active)");
    }

    // Reload settings in case they changed
    loadSettings();

    InputMgr::getInstance().setCallback(std::bind(&AppKlipper::handleInput, this, std::placeholders::_1));

    // Draw initial screen first (full refresh), then start scanning
    draw();

    // Small delay to let display finish before starting network operations
    delay(100);

    // Trigger scan in background task
    startScan();
}

void AppKlipper::stop() {
    Serial.println("Klipper App Stopped");
    // Stop scan task if running
    if (_scanTaskHandle != NULL) {
        vTaskDelete(_scanTaskHandle);
        _scanTaskHandle = NULL;
    }
    _scanning = false;
    _printers.clear();
    _foundPrinters.clear();

    // Re-subscribe async_tcp to WDT now that Klipper is inactive
    if (_asyncTcpTask != NULL) {
        esp_task_wdt_add(_asyncTcpTask);
        Serial.println("async_tcp re-subscribed to WDT (Klipper stopped)");
        _asyncTcpTask = NULL;
    }
}

void AppKlipper::handleInput(InputAction action) {
    if (action == INPUT_NEXT) {
        if (_state == KLIPPER_SCANNING && !_scanning) {
            // Single press in scanning view: start manual scan
            startScan();
        }
        else if (_state == KLIPPER_LIST) {
            // Single press in list view: refresh status
            for (auto& printer : _printers) {
                fetchPrinterStatus(printer);
            }
            _needsRedraw = true;
        }
    }
    else if (action == INPUT_SELECT) {
        // Long press consistently returns to the main menu across apps.
        AppMgr::getInstance().switchTo(0);
    }
}

void AppKlipper::update() {
    unsigned long now = millis();

    // Check if background scan completed
    if (_scanComplete) {
        _scanComplete = false;
        _scanning = false;

        // Transfer found printers to main list
        _printers = _foundPrinters;
        _foundPrinters.clear();

        // Clean up task handle
        _scanTaskHandle = NULL;

        Serial.printf("Scan complete: found %d printers\n", (int)_printers.size());

        // Transition to list view if we found printers
        if (!_printers.empty()) {
            _state = KLIPPER_LIST;
            _firstDraw = true;
            // Fetch initial status for all printers
            for (auto& printer : _printers) {
                fetchPrinterStatus(printer);
            }
        }

        _needsRedraw = true;
    }

    // E-ink progress updates are intentionally coarse: repaint only after
    // another ten addresses have completed instead of flashing for every IP.
    if (_scanning && (_scannedIPs / 10) > _lastDisplayedScanChunk) {
        _lastDisplayedScanChunk = _scannedIPs / 10;
        _needsRedraw = true;
    }

    // Periodic status update in list view (partial refresh)
    unsigned long updateIntervalMs = _statusUpdateInterval * 1000UL;
    if (_state == KLIPPER_LIST && !_printers.empty() && (now - _lastUpdateTime > updateIntervalMs)) {
        for (auto& printer : _printers) {
            fetchPrinterStatus(printer);
        }
        _lastUpdateTime = now;
        _needsRedraw = true;
    }
}

void AppKlipper::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    switch (_state) {
        case KLIPPER_SCANNING:
            drawScanning();
            break;
        case KLIPPER_LIST:
            drawPrinterList();
            break;
    }
}

const uint8_t* AppKlipper::getIconImage() {
    return icon_klipper_160x160;
}

void AppKlipper::forceRedraw() {
    _firstDraw = true;  // Full repaint at the new orientation
    _needsRedraw = true;
}

void AppKlipper::startScan() {
    // Don't start a new scan if one is already running
    if (_scanning || _scanTaskHandle != NULL) {
        Serial.println("Scan already in progress");
        return;
    }

    // Verify WiFi is connected before scanning
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected!");
        _needsRedraw = true;
        return;
    }

    Serial.println("Starting background scan for Klipper printers...");
    _scanning = true;
    _scanComplete = false;
    _foundPrinters.clear();
    _scanProgress = 0;
    _scannedIPs = 0;
    _lastDisplayedScanChunk = 0;
    _totalIPs = 254;  // Full subnet scan
    _needsRedraw = true;
    _lastScanTime = millis();

    // Create FreeRTOS task on Core 0 (separate from async_tcp on Core 1)
    // Stack size 8KB, priority 0 (lowest, below async_tcp)
    BaseType_t result = xTaskCreatePinnedToCore(
        scanTaskWrapper,    // Task function
        "KlipperScan",      // Task name
        8192,               // Stack size
        this,               // Parameter (this pointer)
        0,                  // Priority 0 (lowest - yields to everything)
        &_scanTaskHandle,   // Task handle
        0                   // Core 0
    );

    if (result != pdPASS) {
        Serial.println("Failed to create scan task!");
        _scanning = false;
        _scanTaskHandle = NULL;
    }
}

void AppKlipper::scanTaskWrapper(void* param) {
    AppKlipper* self = static_cast<AppKlipper*>(param);
    self->scanTask();
    vTaskDelete(NULL);  // Delete this task when done
}

void AppKlipper::scanTask() {
    Serial.println("Scan task started on core " + String(xPortGetCoreID()));

    // Get local IP
    IPAddress localIP = WiFi.localIP();
    Serial.printf("Local IP: %s, scanning full subnet for Moonraker...\n", localIP.toString().c_str());

    // Moonraker default port
    const uint16_t MOONRAKER_PORT = 7125;

    // Get base octets from local IP
    uint8_t oct1 = localIP[0];
    uint8_t oct2 = localIP[1];
    uint8_t oct3 = localIP[2];
    uint8_t myOct4 = localIP[3];

    int found = 0;

    // Scan full subnet (1-254), skip .0 and .255
    for (int i = 1; i <= 254 && found < 10; i++) {
        if (i == myOct4) continue;

        IPAddress target(oct1, oct2, oct3, i);
        String targetStr = target.toString();
        _scannedIPs = i;
        _scanProgress = (i * 100) / 254;

        // Yield generously to let async_tcp and other tasks run (prevents watchdog)
        vTaskDelay(pdMS_TO_TICKS(50));

        // Extra yield every 10 IPs to give async_tcp a solid window
        if (i % 10 == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Probe Moonraker on default port
        if (probeMoonraker(targetStr, MOONRAKER_PORT)) {
            // Check if already in found list
            bool exists = false;
            for (const auto& pr : _foundPrinters) {
                if (pr.ip == targetStr) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                PrinterInfo printer;
                printer.ip = targetStr;
                printer.port = MOONRAKER_PORT;
                printer.hostname = "Klipper";
                printer.state = "unknown";
                printer.extruderTemp = 0;
                printer.extruderTarget = 0;
                printer.bedTemp = 0;
                printer.bedTarget = 0;
                printer.progress = 0;
                printer.filename = "";

                // Try to get printer name from Moonraker database (where Mainsail stores it)
                vTaskDelay(pdMS_TO_TICKS(5));
                String dbUrl = "http://" + printer.ip + ":" + String(printer.port) + "/server/database/item?namespace=mainsail";
                String dbResponse = httpGet(dbUrl);
                if (dbResponse.length() > 0) {
                    DynamicJsonDocument doc(4096);
                    if (deserializeJson(doc, dbResponse) == DeserializationError::Ok) {
                        const char* printerName = doc["result"]["value"]["general"]["printername"] | nullptr;
                        if (printerName && strlen(printerName) > 0) {
                            printer.hostname = printerName;
                            Serial.printf("Got printer name from Mainsail DB: %s\n", printerName);
                        }
                    }
                }

                // If we didn't get a custom name, try /printer/info for hostname
                if (printer.hostname == "Klipper") {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    String infoUrl = "http://" + printer.ip + ":" + String(printer.port) + "/printer/info";
                    String infoResponse = httpGet(infoUrl);
                    if (infoResponse.length() > 0) {
                        DynamicJsonDocument doc(2048);
                        if (deserializeJson(doc, infoResponse) == DeserializationError::Ok) {
                            const char* hostname = doc["result"]["hostname"] | "Klipper";
                            printer.hostname = hostname;
                            Serial.printf("Got hostname from /printer/info: %s\n", hostname);
                        }
                    }
                }

                Serial.printf("✓ Found: %s at %s:%d\n",
                             printer.hostname.c_str(), printer.ip.c_str(), printer.port);
                _foundPrinters.push_back(printer);
                found++;
            }
        }

        // Progress feedback every 25 IPs
        if (i % 25 == 0) {
            Serial.printf("Scanned %d/254 IPs (%d%%), found %d printers\n", i, _scanProgress, found);
        }
    }

    Serial.printf("Scan task complete: 254 IPs scanned, %d printers found\n", found);

    // Signal completion to main thread
    _scanComplete = true;
}

bool AppKlipper::probeMoonraker(const String& ip, uint16_t port) {
    // Skip the TCP probe - go straight to HTTP verification.
    // If the port is closed, connect will fail immediately (RST).
    // This avoids the double-connect overhead that was unreliable under load.
    String url = "http://" + ip + ":" + String(port) + "/server/info";
    HTTPClient http;
    http.setConnectTimeout(500);   // 500ms connect timeout (was 100ms TCP probe)
    http.setTimeout(1500);         // 1.5 second read timeout

    if (!http.begin(url)) {
        return false;
    }

    int httpCode = http.GET();
    http.end();

    vTaskDelay(pdMS_TO_TICKS(20));  // Let other tasks run after HTTP

    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("✓ Moonraker found at %s:%d\n", ip.c_str(), port);
        return true;
    }

    return false;
}

void AppKlipper::fetchPrinterStatus(PrinterInfo& printer) {
    Serial.printf("Fetching status for %s\n", printer.hostname.c_str());

    // Query Moonraker API for printer objects - include virtual_sdcard for accurate progress
    String url = "http://" + printer.ip + ":" + String(printer.port) +
                 "/printer/objects/query?heater_bed&extruder&print_stats&virtual_sdcard";

    String response = httpGet(url);
    if (response.length() == 0) {
        printer.state = "offline";
        return;
    }

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        printer.state = "error";
        return;
    }

    // Extract data from response
    JsonObject result = doc["result"]["status"];

    // Extruder temp
    if (result.containsKey("extruder")) {
        printer.extruderTemp = result["extruder"]["temperature"] | 0.0f;
        printer.extruderTarget = result["extruder"]["target"] | 0.0f;
    }

    // Bed temp
    if (result.containsKey("heater_bed")) {
        printer.bedTemp = result["heater_bed"]["temperature"] | 0.0f;
        printer.bedTarget = result["heater_bed"]["target"] | 0.0f;
    }

    // Print stats
    if (result.containsKey("print_stats")) {
        const char* state = result["print_stats"]["state"] | "unknown";
        printer.state = state;
        const char* filename = result["print_stats"]["filename"] | "";
        printer.filename = filename;
    }

    // Get progress from virtual_sdcard (more accurate than calculating from duration)
    if (result.containsKey("virtual_sdcard")) {
        printer.progress = result["virtual_sdcard"]["progress"] | 0.0f;
        printer.progress *= 100.0f;  // Convert 0-1 to 0-100
    }

    Serial.printf("Status: %s, Extruder: %.1f/%.1f, Bed: %.1f/%.1f, Progress: %.1f%%\n",
                  printer.state.c_str(), printer.extruderTemp, printer.extruderTarget,
                  printer.bedTemp, printer.bedTarget, printer.progress);
}

String AppKlipper::httpGet(const String& url) {
    HTTPClient http;
    http.setTimeout(5000);  // 5 second timeout

    if (!http.begin(url)) {
        Serial.println("HTTP begin failed");
        return "";
    }

    int httpCode = http.GET();
    String payload = "";

    if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
    } else {
        Serial.printf("HTTP GET failed: %d\n", httpCode);
    }

    http.end();
    return payload;
}

void AppKlipper::drawScanning() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int screenW = display.width();   // 480
    int screenH = display.height();  // 800

    // Use full refresh on first draw, partial otherwise
    if (_firstDraw) {
        display.setFullWindow();
        _lastFullRefreshTime = millis();
        _firstDraw = false;
    } else {
        display.setPartialWindow(30, screenH / 2 - 95, screenW - 60, 150);
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header
        fontMgr.drawText(display, "Klipper", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(0, 50, screenW, 50, GxEPD_BLACK);

        if (_scanning) {
            // Centered "Scanning..." text with IP count
            char scanText[48];
            snprintf(scanText, sizeof(scanText), "Scanning... %d / %d IPs", (int)_scannedIPs, _totalIPs);
            fontMgr.drawTextCentered(display, scanText, screenH / 2 - 60, FONT_SIZE_MENU, GxEPD_BLACK);

            // Progress bar - centered, clean design
            int barWidth = screenW - 80;
            int barHeight = 24;
            int barX = 40;
            int barY = screenH / 2 - 12;

            display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
            int fillWidth = (_scanProgress * (barWidth - 4)) / 100;
            if (fillWidth > 0) {
                display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, GxEPD_BLACK);
            }

        } else if (_printers.empty()) {
            // No printers found - simple message
            fontMgr.drawTextCentered(display, "No printers found", screenH / 2 - 40, FONT_SIZE_MENU, GxEPD_BLACK);
            fontMgr.drawTextCentered(display, "Press to scan again", screenH / 2 + 20, FONT_SIZE_BODY, GxEPD_BLACK);
        }

        // Footer
        fontMgr.drawTextCentered(display, "Press: Scan  |  Hold: Menu", screenH - 30, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}

void AppKlipper::drawPrinterList() {
    if (_printers.empty()) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int screenW = display.width();   // 480
    int screenH = display.height();  // 800

    // Determine if we need a full refresh
    unsigned long now = millis();
    bool doFullRefresh = _firstDraw;

    // Check if full refresh interval has elapsed (if interval > 0)
    if (!doFullRefresh && _fullRefreshInterval > 0) {
        unsigned long intervalMs = (unsigned long)_fullRefreshInterval * 60 * 1000;
        if (now - _lastFullRefreshTime >= intervalMs) {
            doFullRefresh = true;
        }
    }

    if (doFullRefresh) {
        Serial.printf("Klipper: Full refresh (interval=%d min)\n", _fullRefreshInterval);
        display.setFullWindow();
        _lastFullRefreshTime = now;
        _firstDraw = false;
    } else {
        display.setPartialWindow(0, 50, screenW, screenH - 100);
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        const int MARGIN = 10;
        const int CARD_HEIGHT = 145;  // Height per printer card (fits 4 on screen)
        int y = 0;

        // === Header ===
        y = 35;
        fontMgr.drawText(display, "Klipper Printers", MARGIN, y, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

        // Battery display removed from Klipper screen

        y += 10;
        display.drawLine(0, y, screenW, y, GxEPD_BLACK);

        // === Found X printer(s) ===
        y += 22;
        char foundStr[32];
        snprintf(foundStr, sizeof(foundStr), "Found %d printer(s):", (int)_printers.size());
        fontMgr.drawText(display, foundStr, MARGIN, y, FONT_SIZE_SMALL, GxEPD_BLACK);
        y += 10;

        // === Printer Cards ===
        int cardY = y;
        int printedCount = 0;

        for (size_t i = 0; i < _printers.size() && printedCount < 4; i++) {
            const PrinterInfo& printer = _printers[i];
            int cardTop = cardY + (printedCount * CARD_HEIGHT);

            // Don't draw if card would go past footer area
            if (cardTop + CARD_HEIGHT > screenH - 50) break;

            // Card border
            display.drawRect(MARGIN, cardTop, screenW - (MARGIN * 2), CARD_HEIGHT - 5, GxEPD_BLACK);

            int lineY = cardTop + 22;
            int textX = MARGIN + 8;
            int rightX = screenW - MARGIN - 8;

            // Line 1: Hostname + [status]
            fontMgr.drawText(display, printer.hostname.c_str(), textX, lineY, FONT_SIZE_MENU, GxEPD_BLACK);
            String statusStr = "[" + printer.state + "]";
            fontMgr.drawTextRight(display, statusStr.c_str(), rightX, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);

            // Line 2: IP address
            lineY += 24;
            String ipLine = "IP: " + printer.ip;
            fontMgr.drawText(display, ipLine.c_str(), textX, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);

            // Line 3: Extruder temp
            lineY += 22;
            char extStr[48];
            snprintf(extStr, sizeof(extStr), "Extruder: %.1fC / %.1fC", printer.extruderTemp, printer.extruderTarget);
            fontMgr.drawText(display, extStr, textX, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);

            // Line 4: Bed temp
            lineY += 22;
            char bedStr[48];
            snprintf(bedStr, sizeof(bedStr), "Bed: %.1fC / %.1fC", printer.bedTemp, printer.bedTarget);
            fontMgr.drawText(display, bedStr, textX, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);

            // Line 5: Progress bar (if printing) or filename
            lineY += 22;
            if (printer.state == "printing" && printer.progress > 0) {
                // Mini progress bar
                int barX = textX;
                int barW = 150;
                int barH = 12;
                display.drawRect(barX, lineY - 10, barW, barH, GxEPD_BLACK);
                int fillW = (int)(printer.progress * (barW - 2) / 100.0f);
                if (fillW > 0) display.fillRect(barX + 1, lineY - 9, fillW, barH - 2, GxEPD_BLACK);

                // Progress % and filename
                char progStr[64];
                String fname = printer.filename;
                if (fname.length() > 15) fname = fname.substring(0, 12) + "...";
                snprintf(progStr, sizeof(progStr), "%.0f%% %s", printer.progress, fname.c_str());
                fontMgr.drawText(display, progStr, barX + barW + 8, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);
            } else if (printer.filename.length() > 0) {
                String fname = "File: " + printer.filename;
                if (fname.length() > 35) fname = fname.substring(0, 32) + "...";
                fontMgr.drawText(display, fname.c_str(), textX, lineY, FONT_SIZE_SMALL, GxEPD_BLACK);
            }

            printedCount++;
        }

        // === Footer ===
        fontMgr.drawTextCentered(display, "Press: Refresh  |  Hold: Menu", screenH - 30, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}

void AppKlipper::drawPrinterDetails() {
    if (_printers.empty() || _selectedIndex >= (int)_printers.size()) return;

    PrinterInfo& printer = _printers[_selectedIndex];
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Header - hostname
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print(printer.hostname);
        display.drawLine(0, 45, display.width(), 45, GxEPD_BLACK);

        // IP Address
        display.setTextSize(2);
        display.setCursor(10, 60);
        display.print("IP: ");
        display.print(printer.ip);
        display.print(":");
        display.print(printer.port);

        // Status
        display.setTextSize(3);
        display.setCursor(10, 100);
        display.print("Status: ");
        // Capitalize first letter
        String stateDisplay = printer.state;
        if (stateDisplay.length() > 0) {
            stateDisplay[0] = toupper(stateDisplay[0]);
        }
        display.print(stateDisplay);

        // Temperatures section
        display.drawLine(0, 150, display.width(), 150, GxEPD_BLACK);

        display.setTextSize(2);
        display.setCursor(10, 170);
        display.print("TEMPERATURES");

        // Extruder
        display.setTextSize(3);
        display.setCursor(10, 210);
        display.print("Extruder:");
        display.setCursor(200, 210);
        display.print((int)printer.extruderTemp);
        display.print("/");
        display.print((int)printer.extruderTarget);
        display.setTextSize(2);
        display.print(" C");

        // Bed
        display.setTextSize(3);
        display.setCursor(10, 260);
        display.print("Bed:");
        display.setCursor(200, 260);
        display.print((int)printer.bedTemp);
        display.print("/");
        display.print((int)printer.bedTarget);
        display.setTextSize(2);
        display.print(" C");

        // Print info if printing
        if (printer.state == "printing" && printer.filename.length() > 0) {
            display.drawLine(0, 320, display.width(), 320, GxEPD_BLACK);

            display.setTextSize(2);
            display.setCursor(10, 340);
            display.print("PRINT JOB");

            display.setTextSize(2);
            display.setCursor(10, 380);
            // Truncate filename if too long
            String fname = printer.filename;
            if (fname.length() > 30) {
                fname = fname.substring(0, 27) + "...";
            }
            display.print(fname);

            // Progress bar
            display.setCursor(10, 420);
            display.print("Progress: ");
            display.print((int)printer.progress);
            display.print("%");

            // Draw progress bar
            int barWidth = display.width() - 60;
            int barHeight = 25;
            int barX = 30;
            int barY = 460;
            display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
            int fillWidth = (printer.progress * (barWidth - 4)) / 100;
            if (fillWidth > 0) {
                display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, GxEPD_BLACK);
            }
        }

        // Footer
        display.setTextSize(2);
        display.setCursor(10, display.height() - 30);
        display.print("Press to return | Next for others");

    } while (display.nextPage());
}
