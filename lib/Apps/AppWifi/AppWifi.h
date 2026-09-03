#pragma once

#if defined(BOARD_SEEED_STICKY)

#include "../../Book32_Core/BaseApp.h"
#include "../../Book32_Core/InputMgr.h"
#include <Arduino.h>
#include <vector>

struct WifiNetworkEntry {
    String ssid;
    int32_t rssi;
    uint8_t auth;
};

class AppWifi : public App {
public:
    const char* getName() override { return "Wi-Fi"; }

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);

private:
    enum View {
        NETWORK_LIST,
        PASSWORD_KEYBOARD,
        CONNECTION_RESULT
    };

    View _view = NETWORK_LIST;
    std::vector<WifiNetworkEntry> _networks;
    int _page = 0;
    String _selectedSsid;
    uint8_t _selectedAuth = 0;
    String _password;
    String _status;
    bool _scanning = false;
    bool _connecting = false;
    bool _uppercase = false;
    bool _symbols = false;
    bool _showPassword = false;
    bool _needsRedraw = false;
    bool _fullRefresh = true;
    bool _passwordOnlyRedraw = false;
    unsigned long _lastPoll = 0;
    unsigned long _connectStarted = 0;

    void startScan();
    void finishScan(int count);
    void selectNetwork(int index);
    void beginConnect();
    void returnToMenu();
    void appendKey(char key);
    void drawNetworkList();
    void drawKeyboard();
    void drawConnectionResult();
};

#endif
