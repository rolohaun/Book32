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
#if BOOK32_HAS_INKBOY
#include "NetworkWork.h"
#endif

#if BOOK32_COMPACT_UI
// RLCD (300x400): half-size icons, three to a row.
static constexpr int MENU_START_Y = 72, MENU_ROW_HEIGHT = 128, MENU_ICON_SIZE = 80, MENU_COLS = 3;
#elif defined(BOARD_SEEED_STICKY)
// With Inkade the Sticky and Book32 have five tiles: three tighter rows end above the
// footer on its 800 px screen.
static constexpr int MENU_START_Y = 120, MENU_ROW_HEIGHT = 200, MENU_ICON_SIZE = 160, MENU_COLS = 2;
#else
static constexpr int MENU_START_Y = 180, MENU_ROW_HEIGHT = 240, MENU_ICON_SIZE = 160, MENU_COLS = 2;
#endif
// Selection frame and label spacing scale with the icon.
static constexpr int MENU_FRAME = MENU_ICON_SIZE / 20, MENU_LABEL_DY = MENU_ICON_SIZE == 160 ? 25 : 22;

// The 160x160 app icons, at MENU_ICON_SIZE: each output pixel is black when
// at least half of the source pixels it covers are.
static void drawMenuIcon(Book32Display& display, int x, int y, const uint8_t* icon) {
    if (MENU_ICON_SIZE == 160) {
        display.drawBitmap(x, y, icon, 160, 160, GxEPD_BLACK);
        return;
    }
    constexpr int SCALE = 160 / MENU_ICON_SIZE, PITCH = 160 / 8;
    for (int oy = 0; oy < MENU_ICON_SIZE; ++oy) {
        for (int ox = 0; ox < MENU_ICON_SIZE; ++ox) {
            int black = 0;
            for (int dy = 0; dy < SCALE; ++dy) {
                for (int dx = 0; dx < SCALE; ++dx) {
                    const int sx = ox * SCALE + dx, sy = oy * SCALE + dy;
                    if (pgm_read_byte(icon + sy * PITCH + sx / 8) & (0x80 >> (sx & 7))) ++black;
                }
            }
            if (black * 2 >= SCALE * SCALE) display.drawPixel(x + ox, y + oy, GxEPD_BLACK);
        }
    }
}

#if BOOK32_HAS_INKBOY
// Inkade has no bitmap: a game pad, laid out for a 160 px tile and scaled.
// A 4 px frame as a filled shape minus its inside: nested outlines with one
// radius leave gaps in the corners.
static void drawInkadeIcon(Book32Display& display, int x, int y) {
    auto v = [](int value) { return value * MENU_ICON_SIZE / 160; };
    display.fillRoundRect(x+v(10),y+v(32),v(140),v(98),v(24),GxEPD_BLACK);
    display.fillRoundRect(x+v(14),y+v(36),v(132),v(90),v(20),GxEPD_WHITE);
    display.fillRoundRect(x+v(35),y+v(65),v(46),v(14),v(3),GxEPD_BLACK);
    display.fillRoundRect(x+v(51),y+v(49),v(14),v(46),v(3),GxEPD_BLACK);
    display.fillCircle(x+v(119),y+v(63),v(10),GxEPD_BLACK);
    display.fillCircle(x+v(98),y+v(86),v(10),GxEPD_BLACK);
    display.fillRoundRect(x+v(61),y+v(111),v(17),v(5),v(2),GxEPD_BLACK);
    display.fillRoundRect(x+v(88),y+v(111),v(17),v(5),v(2),GxEPD_BLACK);
}
#endif

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static MenuDirtyRect menuItemRect(int index, int screenW) {
    const int ICON_SIZE = MENU_ICON_SIZE;
    const int COLS = MENU_COLS;
    const int ROW_HEIGHT = MENU_ROW_HEIGHT;
    const int START_Y = MENU_START_Y;
    int colWidth = screenW / COLS;
    int idx = index - 1;
    int col = idx % COLS;
    int row = idx / COLS;
    int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
    int y = START_Y + row * ROW_HEIGHT;
    const int pad = MENU_FRAME + 6;
    return {x - pad, y - pad, ICON_SIZE + 2 * pad, ICON_SIZE + pad + MENU_LABEL_DY + 31};
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
    return current && (strcmp(current->getName(), "eReader") == 0 || strcmp(current->getName(), "Inkade") == 0);
}

void AppMainMenu::updateCheckTask(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    
    // Wait for connection (max 10s)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
#if BOOK32_HAS_INKBOY
        if (NetworkWork::gate().paused()) break;
#endif
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
#if BOOK32_HAS_INKBOY
    {
        NetworkWork::Lease network;
        if (network) wifiWakeWork(parameter);
        else {
            auto* self=static_cast<AppMainMenu*>(parameter);
            self->_wifiStarting=false;
            self->_wifiTaskHandle=nullptr;
        }
    } // Destroy the lease BEFORE vTaskDelete (which does not unwind C++).
#else
    wifiWakeWork(parameter);
#endif
    vTaskDelete(nullptr);
}

void AppMainMenu::wifiWakeWork(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    Serial.println("Main menu WiFi wake task started");

    if (isReaderActive()) {
        self->_wifiStarting = false;
        self->_wifiTaskHandle = nullptr;
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
#if BOOK32_HAS_INKBOY
        if (NetworkWork::gate().paused()) {
            self->_wifiStarting=false;
            self->_wifiTaskHandle=nullptr;
            return; // The offline app shuts down radio after all owners drain.
        }
#endif
#if BOOK32_HAS_TOUCH
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current && strcmp(current->getName(), "Settings") == 0) {
            self->_wifiStarting = false;
            self->_wifiTaskHandle = nullptr;
            Serial.println("Main menu WiFi wake handed radio to Settings app");
            return;
        }
#endif
        if (isReaderActive()) {
#if BOOK32_HAS_INKBOY
            App* owner=AppMgr::getInstance().getCurrentApp();
            if (owner && strcmp(owner->getName(),"Inkade")==0) {
                self->_wifiStarting=false;self->_wifiTaskHandle=nullptr;
                return; // Inkade alone drains HTTPS then powers down radio.
            }
#endif
            WebMgr::getInstance().stop();
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        self->_wifiTaskHandle = nullptr;
            Serial.println("Main menu WiFi wake cancelled; eReader is active");
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
#if !BOOK32_HAS_TOUCH
        if (!isReaderActive()) self->startHotspot();
#else
        Serial.println("Sticky is offline; use Wi-Fi inside the on-device Settings app");
#endif
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        return;
    }

    self->_wifiStarting = false;
    self->_footerOnlyRedraw = true;
    self->_needsRedraw = true;
    self->_wifiTaskHandle = nullptr;
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

#if BOOK32_HAS_TOUCH
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
    if (BOOK32_HAS_OTA && !_updateTaskHandle && !_updateAvailable) {
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
#if BOOK32_HAS_TOUCH
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
#if BOOK32_HAS_TOUCH
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
#if BOOK32_HAS_TOUCH
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
#if defined(BOARD_WAVESHARE_RLCD42)
    else if (action == INPUT_PREV) {  // BOOT
        _previousSelectedIndex = selectedIndex;
        if (--selectedIndex < 1) selectedIndex = maxSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
#endif
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
#if !BOOK32_HAS_TOUCH
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
#if BOOK32_HAS_TOUCH
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       status.percentage != _lastBatteryStatus.percentage ||
                       fabsf(status.voltage - _lastBatteryStatus.voltage) >= 0.03f;
#else
        // The XIAO's ADC reading wobbles by a percent or two, and every
        // partial update drives the whole 7.5" panel. Only repaint the
        // icon-only indicator for a visible change.
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       abs(status.percentage - _lastBatteryStatus.percentage) >= 5 ||
                       (status.percentage == 0) != (_lastBatteryStatus.percentage == 0);
#endif
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
    const int ICON_SIZE = MENU_ICON_SIZE;
    const int COLS = MENU_COLS;
    const int ROW_HEIGHT = MENU_ROW_HEIGHT;
    const int START_Y = MENU_START_Y;

#if !BOOK32_HAS_TOUCH
    // Partial updates slowly accumulate ghosting on the 7.5" panel; clear it
    // with an occasional full refresh, as the reader does every N pages.
    if (!_firstDraw && ++_partialRefreshes >= MENU_FULL_REFRESH_INTERVAL) {
        _firstDraw = true;
        _selectionOnlyRedraw = false;
        _batteryOnlyRedraw = false;
        _footerOnlyRedraw = false;
    }
    if (_firstDraw) _partialRefreshes = 0;
#endif

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

        // === Firmware title and version on both devices ===
        fontMgr.drawText(display, "InkDeck", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int inkDeckWidth = fontMgr.getTextWidth("InkDeck", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + inkDeckWidth, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

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

#if !BOOK32_HAS_TOUCH
            if ((int)i == selectedIndex) {
                const int f = MENU_FRAME;
                display.drawRect(x - f, y - f, ICON_SIZE + 2 * f, ICON_SIZE + 2 * f, GxEPD_BLACK);
                display.drawRect(x - f + 1, y - f + 1, ICON_SIZE + 2 * f - 2, ICON_SIZE + 2 * f - 2, GxEPD_BLACK);
            }
#endif

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                drawMenuIcon(display, x, y, icon);
#if BOOK32_HAS_INKBOY
            } else if (strcmp(app->getName(), "Inkade") == 0) {
                drawInkadeIcon(display, x, y);
#endif
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

#if BOOK32_HAS_TOUCH
            String name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
#else
            const char* name = app->getName();
            const int nameSize = BOOK32_COMPACT_UI ? FONT_SIZE_SMALL : FONT_SIZE_MENU;
            int nameWidth = fontMgr.getTextWidth(name, nameSize);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + MENU_LABEL_DY, nameSize, GxEPD_BLACK);
#endif
        }
        
        // The original Book32 keeps its virtual update tile. Sticky keeps its
        // fourth tile dedicated to Settings and installs updates from the web UI.
#if !BOOK32_HAS_TOUCH
        if (_updateAvailable) {
            int i = apps.size(); // Index for update app (virtual index)
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;
            
            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;
            
             if ((int)i == selectedIndex) {
                 // Selection Box
                const int f = MENU_FRAME;
                display.drawRect(x - f, y - f, ICON_SIZE + 2 * f, ICON_SIZE + 2 * f, GxEPD_BLACK);
                display.drawRect(x - f + 1, y - f + 1, ICON_SIZE + 2 * f - 2, ICON_SIZE + 2 * f - 2, GxEPD_BLACK);
            }
            
            drawMenuIcon(display, x, y, icon_update_160x160);
            
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
