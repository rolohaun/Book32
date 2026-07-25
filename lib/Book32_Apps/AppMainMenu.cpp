#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Core/DeviceCred.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>
#include "icon_update.h"
#include "../Book32_Update/GitHubMgr.h"

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static MenuDirtyRect menuItemRect(int index, int screenW) {
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;
    int colWidth = screenW / COLS;
    int idx = index - 1;
    int col = idx % COLS;
    int row = idx / COLS;
    int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
    int y = START_Y + row * ROW_HEIGHT;
    return {x - 14, y - 14, ICON_SIZE + 28, ICON_SIZE + 70};
}

static MenuDirtyRect unionRect(MenuDirtyRect a, MenuDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

static bool isReaderActive() {
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && strcmp(current->getName(), "eReader") == 0;
}

void AppMainMenu::updateCheckTask(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    
    // Wait for connection (max 10s)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);
        if (info.available) {
            self->_updateAvailable = true;
            self->_updateVersion = info.version;
            self->_needsRedraw = true; // Trigger redraw to show icon
        }
    }
    
    self->_updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void AppMainMenu::wifiWakeTask(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    Serial.println("Main menu WiFi wake task started");

    if (isReaderActive()) {
        self->_wifiStarting = false;
        self->_wifiTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        if (isReaderActive()) {
            WebMgr::getInstance().stop();
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        self->_wifiTaskHandle = nullptr;
            Serial.println("Main menu WiFi wake cancelled; eReader is active");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED && !isReaderActive()) {
        Serial.println("Main menu WiFi connected");
        Serial.println(WiFi.localIP());
        WebMgr::getInstance().init();
    } else {
        Serial.println("Main menu WiFi wake did not connect; bringing up hotspot");
        self->_wifiTaskHandle = nullptr;  // Clear before starting the hotspot
        if (!isReaderActive()) self->startHotspot();
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        vTaskDelete(NULL);
        return;
    }

    self->_wifiStarting = false;
    self->_footerOnlyRedraw = true;
    self->_needsRedraw = true;
    self->_wifiTaskHandle = nullptr;
    vTaskDelete(NULL);
}

String AppMainMenu::getWifiFooterText() const {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != INADDR_NONE) {
            return ip.toString();
        }
    }
    if (_hotspotActive) {
        // Show the passphrase only while the hotspot is up. On a normal
        // station connection the credential stays off-screen, so simply
        // picking the device up doesn't reveal the API password.
        return String("Wi-Fi: ") + AP_SSID + " / " + WebMgr::devicePassword() +
               "  ->  192.168.4.1";
    }
    return _wifiStarting ? "WiFi starting" : "WiFi offline";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting Book32 management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);  // AP serves the web UI; STA stays available for joining a network
    // v1.5.0 (security): the hotspot was previously open, giving anyone in
    // radio range full access to the API. WPA2 needs >= 8 characters, which
    // DeviceCred.h's fixed credential satisfies. The passphrase is still shown
    // in the footer while the hotspot is up, as a reminder.
    WiFi.softAP(AP_SSID, WebMgr::devicePassword());
    delay(100);  // Let the AP interface come up before binding the server
    WebMgr::getInstance().init();
    _hotspotActive = true;

    Serial.print("Hotspot ready at ");
    Serial.println(WiFi.softAPIP());

    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = !_firstDraw;
    _needsRedraw = true;
}

void AppMainMenu::stopHotspot() {
    if (!_hotspotActive) return;

    Serial.println("Main menu: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    // Drop back to station-only; preserves an active connection if one exists.
    WiFi.mode(WIFI_STA);
    _hotspotActive = false;
}

void AppMainMenu::ensureWifiAwake() {
    if (WiFi.status() == WL_CONNECTED) {
        WebMgr::getInstance().init();
        _wifiStarting = false;
        return;
    }

    if (gNetworkStartupInProgress) {
        _wifiStarting = true;
        return;
    }

    if (!_wifiTaskHandle) {
        _wifiStarting = true;
        xTaskCreatePinnedToCore(wifiWakeTask, "WiFiWake", 6144, this, 1, &_wifiTaskHandle, 0);
    }
}

void AppMainMenu::start() {
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;
    _lastWifiConnected = WiFi.status() == WL_CONNECTED;
    _lastIp = _lastWifiConnected ? WiFi.localIP().toString() : "";
    _lastWifiFooterText = "";
    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
    ensureWifiAwake();
    
    // Spawn update check task if not already found
    if (!_updateTaskHandle && !_updateAvailable) {
        xTaskCreatePinnedToCore(updateCheckTask, "UpdateCheck", 8192, this, 1, &_updateTaskHandle, 0);
    }
}

void AppMainMenu::stop() {
    // Hotspot is a main-menu-only convenience. Leaving the menu tears it down so
    // it doesn't keep the radio (and battery) busy inside other apps. Normal
    // station connections are left untouched for management services.
    stopHotspot();
}

void AppMainMenu::forceRedraw() {
    _firstDraw = true;  // Full-frame repaint at the new orientation
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _needsRedraw = true;
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
    
    Serial.printf("AppMainMenu::handleInput - action: %d\n", action);
    
    // Max index is apps.size() - 1 + 1 (if update available)
    int maxIndex = apps.size() - 1 + (_updateAvailable ? 1 : 0); // 0-based index? No selectedIndex is 1-based (starts at 1)
    // Actually selectedIndex starts at 1. App 1 is index 1.
    // apps[0] is MainMenu. apps[1]...apps[N-1] are apps.
    // Update button would be index N (apps.size())
    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);

    if (action == INPUT_NEXT) {
        _previousSelectedIndex = selectedIndex;
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1; // Should not happen but safety
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (_updateAvailable && selectedIndex == (int)apps.size()) {
            // Update selected
             GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
        }
        else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
    else if (action == INPUT_GO_TO_MAIN_MENU) {
        // Already at main menu, no action needed
        Serial.println("AppMainMenu: INPUT_GO_TO_MAIN_MENU - already at main menu");
    }
}

void AppMainMenu::update() {
    unsigned long now = millis();

    if (now - _lastNetworkPoll >= 1000) {
        _lastNetworkPoll = now;

        bool connected = WiFi.status() == WL_CONNECTED;
        String ip = connected ? WiFi.localIP().toString() : "";
        if (connected) {
            _wifiStarting = false;
        } else if (!gNetworkStartupInProgress && !_wifiTaskHandle) {
            _wifiStarting = false;
            // Offline and idle: bring up the management hotspot so a phone can
            // still reach the web interface without a router.
            if (!_hotspotActive && !isReaderActive()) {
                startHotspot();
            }
        }

        String footerText = getWifiFooterText();
        if (connected != _lastWifiConnected || ip != _lastIp || footerText != _lastWifiFooterText) {
            _lastWifiConnected = connected;
            _lastIp = ip;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = false;
            _footerOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }

    if (now - _lastBatteryPoll >= 10000) {
        _lastBatteryPoll = now;
        BatteryStatus status = BatteryMgr::getInstance().refreshNow();
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       status.percentage != _lastBatteryStatus.percentage ||
                       fabsf(status.voltage - _lastBatteryStatus.voltage) >= 0.03f;
        if (changed) {
            _lastBatteryStatus = status;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }
}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int16_t screenW = display.width();   // 480
    int16_t screenH = display.height();  // 800

    // Layout constants
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;

    // Use full refresh only on first draw, partial refresh for navigation
    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        MenuDirtyRect dirty = unionRect(menuItemRect(_previousSelectedIndex, screenW),
                                       menuItemRect(selectedIndex, screenW));
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else if (_batteryOnlyRedraw) {
        display.setPartialWindow(screenW - 150, 0, 150, 42);
    } else if (_footerOnlyRedraw) {
        display.setPartialWindow(0, screenH - 70, screenW, 70);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // === Title (only on full draw, persists on partial) ===
        fontMgr.drawText(display, "Book32", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int book32Width = fontMgr.getTextWidth("Book32", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + book32Width, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        // === Battery Status (single cached read) ===
        BatteryStatus bat = BatteryMgr::getInstance().getStatus();
        int batX = screenW - 60;
        int batY = 10;

        display.drawRect(batX, batY, 40, 20, GxEPD_BLACK);
        display.fillRect(batX + 40, batY + 5, 3, 10, GxEPD_BLACK);

        int fillWidth = (bat.percentage * 36) / 100;
        if(fillWidth > 36) fillWidth = 36;
        if(fillWidth < 0) fillWidth = 0;
        if (bat.percentage > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, 16, GxEPD_BLACK);
        }
        // Draw lightning bolt if charging
        if (bat.charging) {
            display.drawLine(batX + 20, batY + 2, batX + 14, batY + 10, GxEPD_WHITE);
            display.drawLine(batX + 14, batY + 10, batX + 24, batY + 10, GxEPD_WHITE);
            display.drawLine(batX + 24, batY + 10, batX + 18, batY + 18, GxEPD_WHITE);
        }

        // === App Icons Grid ===
        int colWidth = screenW / COLS;

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue;

            App* app = apps[i];
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;

            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;

            if ((int)i == selectedIndex) {
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }
        
        // Render Update Icon if available
        if (_updateAvailable) {
            int i = apps.size(); // Index for update app (virtual index)
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;
            
            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;
            
             if ((int)i == selectedIndex) {
                 // Selection Box
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }
            
            display.drawBitmap(x, y, icon_update_160x160, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            
            String updateText = "Update " + _updateVersion;
            int nameWidth = fontMgr.getTextWidth(updateText.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, updateText.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }

        // === Footer ===
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Select", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}
