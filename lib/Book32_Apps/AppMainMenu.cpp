#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>
#include <qrcode.h>
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

static String activeSetupApSsid() {
    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_AP && mode != WIFI_AP_STA) return "";
    return WiFi.softAPSSID();
}

static String escapeWifiQrField(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 4);
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') escaped += '\\';
        escaped += c;
    }
    return escaped;
}

static bool drawWifiSetupQr(Book32Display& display, const String& ssid,
                            int tileX, int tileY, int tileWidth) {
    constexpr uint8_t QR_VERSION = 3;
    constexpr int QUIET_MODULES = 4;
    constexpr int QR_BOX_SIZE = 185;
    uint8_t modules[256];
    QRCode qr;
    String payload = String("WIFI:T:nopass;S:") + escapeWifiQrField(ssid) + ";;";
    if (qrcode_initText(&qr, modules, QR_VERSION, ECC_LOW, payload.c_str()) != 0) return false;

    int totalModules = qr.size + (QUIET_MODULES * 2);
    int scale = max(1, QR_BOX_SIZE / totalModules);
    int pixelSize = totalModules * scale;
    int originX = tileX + (tileWidth - pixelSize) / 2;
    int originY = tileY;
    display.fillRect(originX, originY, pixelSize, pixelSize, GxEPD_WHITE);

    int moduleX = originX + (QUIET_MODULES * scale);
    int moduleY = originY + (QUIET_MODULES * scale);
    for (uint8_t y = 0; y < qr.size; ++y) {
        for (uint8_t x = 0; x < qr.size; ++x) {
            if (qrcode_getModule(&qr, x, y)) {
                display.fillRect(moduleX + (x * scale), moduleY + (y * scale),
                                 scale, scale, GxEPD_BLACK);
            }
        }
    }
    return true;
}

#if BOOK32_HAS_BUZZER
static constexpr uint8_t MENU_BUZZER_CHANNEL = 7;
#endif

static void playMenuTouchBeep() {
#if BOOK32_HAS_BUZZER
    // A brief confirmation chirp for valid touchscreen choices.
    // It is deliberately owned by the main menu so reading and page turns stay silent.
    ledcWriteTone(MENU_BUZZER_CHANNEL, 2400);
    delay(35);
    ledcWriteTone(MENU_BUZZER_CHANNEL, 0);
#endif
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
        self->_updateAvailable = info.available;
        if (info.available) {
            self->_updateVersion = info.version;
            self->_firstDraw = true; // Cleanly replace a possible Wi-Fi QR tile.
            self->_needsRedraw = true; // Trigger redraw to show icon
        } else {
            self->_updateVersion = "";
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
    String setupSsid = activeSetupApSsid();
    if (setupSsid.length() > 0) {
        return String("Wi-Fi: ") + setupSsid + "  ->  " + WiFi.softAPIP().toString();
    }
    return _wifiStarting ? "WiFi starting" : "WiFi offline";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting Book32 management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);  // AP serves the web UI; STA stays available for joining a network
    WiFi.softAP(AP_SSID);
    delay(100);  // Let the AP interface come up before binding the server
    WebMgr::getInstance().init();
    _hotspotActive = true;
    _lastSetupApSsid = activeSetupApSsid();

    Serial.print("Hotspot ready at ");
    Serial.println(WiFi.softAPIP());

    _firstDraw = true; // The QR tile and footer both changed.
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
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
#if BOOK32_HAS_BUZZER
    ledcSetup(MENU_BUZZER_CHANNEL, 2400, 10);
    ledcAttachPin(PIN_BUZZER, MENU_BUZZER_CHANNEL);
    ledcWrite(MENU_BUZZER_CHANNEL, 0);
#endif
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;
    _lastWifiConnected = WiFi.status() == WL_CONNECTED;
    _lastIp = _lastWifiConnected ? WiFi.localIP().toString() : "";
    _lastSetupApSsid = activeSetupApSsid();
    _lastWifiFooterText = "";
    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppMainMenu::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));
    ensureWifiAwake();
    
    // Spawn update check task if not already found
    if (!_updateTaskHandle && !_updateAvailable) {
        xTaskCreatePinnedToCore(updateCheckTask, "UpdateCheck", 8192, this, 1, &_updateTaskHandle, 0);
    }
}

void AppMainMenu::stop() {
    // Hotspot is a main-menu-only convenience. Leaving the menu tears it down so
    // it doesn't keep the radio (and battery) busy inside other apps. Normal
    // station connections are left untouched (Klipper still needs WiFi).
    stopHotspot();
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppMainMenu::handleTouch(uint16_t x, uint16_t y) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
    int maxSelectable = static_cast<int>(apps.size()) - 1 + (_updateAvailable ? 1 : 0);
    for (int index = 1; index <= maxSelectable; ++index) {
        MenuDirtyRect rect = menuItemRect(index, SCREEN_WIDTH);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            selectedIndex = index;
            playMenuTouchBeep();
            if (_updateAvailable && index == static_cast<int>(apps.size())) {
                GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
            } else if (index < static_cast<int>(apps.size())) {
                appMgr.switchTo(index);
            }
            return;
        }
    }
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
}

void AppMainMenu::update() {
    unsigned long now = millis();

    if (now - _lastNetworkPoll >= 1000) {
        _lastNetworkPoll = now;

        bool connected = WiFi.status() == WL_CONNECTED;
        String ip = connected ? WiFi.localIP().toString() : "";
        String setupApSsid = activeSetupApSsid();
        if (connected) {
            _wifiStarting = false;
        } else if (!gNetworkStartupInProgress && !_wifiTaskHandle) {
            _wifiStarting = false;
            // Offline and idle: bring up the management hotspot so a phone can
            // still reach the web interface without a router.
            if (!_hotspotActive && !isReaderActive()) {
                startHotspot();
                setupApSsid = activeSetupApSsid();
            }
        }

        String footerText = getWifiFooterText();
        if (connected != _lastWifiConnected || ip != _lastIp ||
            setupApSsid != _lastSetupApSsid || footerText != _lastWifiFooterText) {
            bool setupTileChanged = connected != _lastWifiConnected || setupApSsid != _lastSetupApSsid;
            _lastWifiConnected = connected;
            _lastIp = ip;
            _lastSetupApSsid = setupApSsid;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = false;
            if (setupTileChanged) {
                // QR codes need a clean waveform when appearing, changing, or disappearing.
                _firstDraw = true;
                _footerOnlyRedraw = false;
            } else {
                _footerOnlyRedraw = !_firstDraw;
            }
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
        
        // Update owns the fourth tile when available. Otherwise, while offline,
        // show a QR that joins the active setup hotspot.
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
        } else {
            String setupSsid = activeSetupApSsid();
            if (WiFi.status() != WL_CONNECTED && setupSsid.length() > 0) {
                int tileX = colWidth;
                int tileY = START_Y + ROW_HEIGHT;
                if (drawWifiSetupQr(display, setupSsid, tileX, tileY, colWidth)) {
                    const char* setupText = "Scan for Wi-Fi";
                    int nameWidth = fontMgr.getTextWidth(setupText, FONT_SIZE_MENU);
                    int nameX = tileX + (colWidth - nameWidth) / 2;
                    fontMgr.drawText(display, setupText, nameX, tileY + 210,
                                     FONT_SIZE_MENU, GxEPD_BLACK);
                }
            }
        }

        // === Footer ===
#if BOOK32_HAS_TOUCH
        const char* menuHint = (WiFi.status() != WL_CONNECTED && activeSetupApSsid().length() > 0 && !_updateAvailable)
                                   ? "Scan QR to set up Wi-Fi"
                                   : "Tap an icon to open";
        fontMgr.drawTextCentered(display, menuHint, screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
#else
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Select", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
#endif
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}
