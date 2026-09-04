#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Core/SoundMgr.h"
#include "../Book32_Web/WebMgr.h"
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

#if defined(BOARD_SEEED_STICKY)
static void fillQuad(Book32Display& display,
                     int x1, int y1, int x2, int y2,
                     int x3, int y3, int x4, int y4) {
    display.fillTriangle(x1, y1, x2, y2, x3, y3, GxEPD_BLACK);
    display.fillTriangle(x1, y1, x3, y3, x4, y4, GxEPD_BLACK);
}

static void drawSettingsMenuIcon(Book32Display& display, int x, int y) {
    // A framed, eight-tooth cog matching the bold outlined app icons.
    const int cx = x + 80;
    const int cy = y + 80;

    for (int inset = 0; inset < 4; ++inset) {
        display.drawRoundRect(x + 5 + inset, y + 5 + inset,
                              150 - inset * 2, 150 - inset * 2,
                              10, GxEPD_BLACK);
    }

    // Square cardinal teeth.
    display.fillRect(cx - 9, y + 20, 18, 34, GxEPD_BLACK);
    display.fillRect(cx - 9, y + 106, 18, 34, GxEPD_BLACK);
    display.fillRect(x + 20, cy - 9, 34, 18, GxEPD_BLACK);
    display.fillRect(x + 106, cy - 9, 34, 18, GxEPD_BLACK);

    // Square diagonal teeth, each drawn as a solid quadrilateral.
    fillQuad(display, x + 101, y + 47, x + 114, y + 34,
             x + 126, y + 46, x + 113, y + 59);
    fillQuad(display, x + 113, y + 101, x + 126, y + 114,
             x + 114, y + 126, x + 101, y + 113);
    fillQuad(display, x + 59, y + 101, x + 46, y + 114,
             x + 34, y + 126, x + 47, y + 113);
    fillQuad(display, x + 47, y + 59, x + 34, y + 46,
             x + 46, y + 34, x + 59, y + 47);

    // Solid gear body with a clean center opening.
    display.fillCircle(cx, cy, 44, GxEPD_BLACK);
    display.fillCircle(cx, cy, 27, GxEPD_WHITE);
}
#endif

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
#if defined(BOARD_SEEED_STICKY)
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current && strcmp(current->getName(), "Settings") == 0) {
            self->_wifiStarting = false;
            self->_wifiTaskHandle = nullptr;
            Serial.println("Main menu WiFi wake handed radio to Settings app");
            vTaskDelete(NULL);
            return;
        }
#endif
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
        Serial.println("Main menu WiFi wake did not connect");
        self->_wifiTaskHandle = nullptr;  // Clear before starting the hotspot
#if !defined(BOARD_SEEED_STICKY)
        if (!isReaderActive()) self->startHotspot();
#else
        Serial.println("Sticky is offline; use Wi-Fi inside the on-device Settings app");
#endif
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
        return String("Wi-Fi: ") + AP_SSID + "  ->  192.168.4.1";
    }
    return _wifiStarting ? "WiFi starting" : "WiFi offline";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting InkDeck management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);  // AP serves the web UI; STA stays available for joining a network
    WiFi.softAP(AP_SSID);
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

#if defined(BOARD_SEEED_STICKY)
    // A failed or cancelled attempt leaves STA enabled. Do not continually
    // retry it and starve the interactive scanner. Wi-Fi is woken here only
    // after the reader deliberately powered the radio fully off.
    if (WiFi.getMode() != WIFI_OFF) {
        _wifiStarting = false;
        return;
    }
#endif

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
#if defined(BOARD_SEEED_STICKY)
    int maxSelectable = static_cast<int>(apps.size()) - 1;
#else
    int maxSelectable = static_cast<int>(apps.size()) - 1 + (_updateAvailable ? 1 : 0);
    int updateIndex = static_cast<int>(apps.size());
#endif
    for (int index = 1; index <= maxSelectable; ++index) {
        MenuDirtyRect rect = menuItemRect(index, SCREEN_WIDTH);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            selectedIndex = index;
            SoundMgr::getInstance().beep();
#if defined(BOARD_SEEED_STICKY)
            if (index < static_cast<int>(apps.size())) appMgr.switchTo(index);
#else
            if (_updateAvailable && index == updateIndex) {
                GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
            } else if (index < static_cast<int>(apps.size())) {
                appMgr.switchTo(index);
            }
#endif
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
#if defined(BOARD_SEEED_STICKY)
    // Sticky launches apps directly by touch; its hardware buttons are used as
    // back controls inside apps rather than navigating an invisible selection.
    (void)action;
    return;
#else
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);
    int updateIndex = static_cast<int>(apps.size());

    if (action == INPUT_NEXT) {
        _previousSelectedIndex = selectedIndex;
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1; // Should not happen but safety
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (_updateAvailable && selectedIndex == updateIndex) {
            // Update selected
             GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
        }
        else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
#endif
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
#if !defined(BOARD_SEEED_STICKY)
            if (!_hotspotActive && !isReaderActive()) {
                startHotspot();
            }
#endif
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

        // === Title (only on the original Book32; Sticky keeps a clean header) ===
#if !defined(BOARD_SEEED_STICKY)
        fontMgr.drawText(display, "InkDeck", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int inkDeckWidth = fontMgr.getTextWidth("InkDeck", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + inkDeckWidth, 35, FONT_SIZE_SMALL, GxEPD_BLACK);
#endif

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

#if !defined(BOARD_SEEED_STICKY)
            if ((int)i == selectedIndex) {
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }
#endif

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
#if defined(BOARD_SEEED_STICKY)
            } else if (strcmp(app->getName(), "Settings") == 0) {
                drawSettingsMenuIcon(display, x, y);
#endif
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

#if defined(BOARD_SEEED_STICKY)
            String name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
#else
            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
#endif
        }
        
        // The original Book32 keeps its virtual update tile. Sticky keeps its
        // fourth tile dedicated to Settings and installs updates from the web UI.
#if !defined(BOARD_SEEED_STICKY)
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
#endif

        // === Footer ===
#if BOOK32_HAS_TOUCH
        fontMgr.drawTextCentered(display, "Tap an icon to open", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
#else
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Select", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
#endif
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}
