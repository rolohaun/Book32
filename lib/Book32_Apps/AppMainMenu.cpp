#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Core/SoundMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../../include/Config.h"
#include "icon_update.h"
#include "../Book32_Update/GitHubMgr.h"
#include "HomeServices.h"
#include "HomeFooter.h"

// No title: the tiles start near the top; the footer names InkDeck.
#if defined(BOARD_SEEED_STICKY)
// With Inkade the Sticky and Book32 have five tiles: three tighter rows end above the
// footer on its 800 px screen.
static constexpr int MENU_START_Y = 40, MENU_ROW_HEIGHT = 200;
#else
static constexpr int MENU_START_Y = 40, MENU_ROW_HEIGHT = 240;
#endif
// The footer's partial refresh: the Book32's hint line over the home footer.
static constexpr int FOOTER_H = BOOK32_HAS_TOUCH ? HomeFooter::HEIGHT + 6 : HomeFooter::HEIGHT + 34;

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static MenuDirtyRect menuItemRect(int index, int screenW) {
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = MENU_ROW_HEIGHT;
    const int START_Y = MENU_START_Y;
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

bool AppMainMenu::updateAvailable() const { return HomeServices::getInstance().updateAvailable(); }

void AppMainMenu::start() {
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;
    _lastWifiFooterText = "";
    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppMainMenu::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));
    HomeServices::getInstance().enter();
}

void AppMainMenu::stop() {
    HomeServices::getInstance().leave();
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppMainMenu::handleTouch(uint16_t x, uint16_t y) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
#if BOOK32_HAS_TOUCH
    int maxSelectable = static_cast<int>(apps.size()) - 1;
#else
    int maxSelectable = static_cast<int>(apps.size()) - 1 + (updateAvailable() ? 1 : 0);
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
            if (updateAvailable() && index == updateIndex) {
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

    int maxSelectable = apps.size() - 1 + (updateAvailable() ? 1 : 0);
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
        if (updateAvailable() && selectedIndex == updateIndex) {
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

    if (HomeServices::getInstance().poll()) {
        // A new update notice adds a tile; Wi-Fi changes only touch the footer.
        const bool update = updateAvailable() != _shownUpdate;
        _selectionOnlyRedraw = false;
        _batteryOnlyRedraw = false;
        _footerOnlyRedraw = !_firstDraw && !update;
        _needsRedraw = true;
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
    const int ICON_SIZE = 160;
    const int COLS = 2;
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
    } else if (_batteryOnlyRedraw || _footerOnlyRedraw) {  // the battery is in the footer
        display.setPartialWindow(0, screenH - FOOTER_H, screenW, FOOTER_H);
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
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }
#endif

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
#if BOOK32_HAS_INKBOY
            } else if (strcmp(app->getName(), "Inkade") == 0) {
                // A 4 px frame as a filled shape minus its inside: nested
                // outlines with one radius leave gaps in the corners.
                display.fillRoundRect(x+10,y+32,140,98,24,GxEPD_BLACK);
                display.fillRoundRect(x+14,y+36,132,90,20,GxEPD_WHITE);
                display.fillRoundRect(x+35,y+65,46,14,3,GxEPD_BLACK);
                display.fillRoundRect(x+51,y+49,14,46,3,GxEPD_BLACK);
                display.fillCircle(x+119,y+63,10,GxEPD_BLACK);
                display.fillCircle(x+98,y+86,10,GxEPD_BLACK);
                display.fillRoundRect(x+61,y+111,17,5,2,GxEPD_BLACK);
                display.fillRoundRect(x+88,y+111,17,5,2,GxEPD_BLACK);
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
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
#endif
        }

        // The original Book32 keeps its virtual update tile. Sticky keeps its
        // fourth tile dedicated to Settings and installs updates from the web UI.
#if !BOOK32_HAS_TOUCH
        _shownUpdate = updateAvailable();
        if (_shownUpdate) {
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

            String updateText = "Update " + HomeServices::getInstance().updateVersion();
            int nameWidth = fontMgr.getTextWidth(updateText.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, updateText.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }
#endif

        // === Footer: the buttons' hint on the Book32, then the home footer ===
#if !BOOK32_HAS_TOUCH
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Select", screenH - FOOTER_H + 22,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);
#endif
        HomeFooter::draw(display, true);
        _lastWifiFooterText = HomeServices::getInstance().footerText();

    } while (display.nextPage());
}
