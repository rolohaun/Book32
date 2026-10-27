#pragma once
#include "BaseApp.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/BatteryMgr.h"

class AppMainMenu : public App {
public:
    const char* getName() override { return "Main Menu"; }
    
    void start() override;
    void update() override;
    void draw() override;
    void stop() override;
    void forceRedraw() override;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);

private:
    int selectedIndex = 0;
    bool _needsRedraw = false;
    bool _firstDraw = true;
    bool _selectionOnlyRedraw = false;
    bool _batteryOnlyRedraw = false;
    bool _footerOnlyRedraw = false;
    static const int MENU_FULL_REFRESH_INTERVAL = 20;
    int _partialRefreshes = 0;
    int _previousSelectedIndex = 1;
    // Wi-Fi, hotspot and update checks: HomeServices. These track what the
    // screen last showed of them.
    String _lastWifiFooterText = "";
    bool _shownUpdate = false;
    unsigned long _lastBatteryPoll = 0;
    BatteryStatus _lastBatteryStatus = {0.0f, -1, false};
    bool updateAvailable() const;
};
