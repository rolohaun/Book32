#pragma once
#include "../../Book32_Core/SleepDefaults.h"
#include "Config.h"

#if BOOK32_HAS_TOUCH

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
    const char* getName() override { return "Settings"; }
    const uint8_t* getIconImage() override;

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);

private:
    enum View {
        SETTINGS_HOME,
        READER_DISPLAY,
        SLEEP_SCREEN,
        LIGHT_BUTTONS,
        GAMEPAD,
        NETWORK_LIST,
        PASSWORD_KEYBOARD,
        MESSAGE_KEYBOARD,
        CONNECTION_RESULT
    };

    View _view = SETTINGS_HOME;
    std::vector<WifiNetworkEntry> _networks;
    int _page = 0;
    String _selectedSsid;
    uint8_t _selectedAuth = 0;
    String _password;
    String _sleepMessage;
    String _messageDraft;
    String _status;
    int _sleepTimeoutMinutes = SLEEP_TIMEOUT_DEFAULT_MINUTES;
    SleepScreen _sleepScreen = SleepScreen::COVER_AND_MESSAGE;
    int _fontSizePt = 18;
    int _rotation = 3;
    bool _showChapter = true;
    bool _showPageNumber = true;
    bool _showReadingPercentage = true;
    bool _mangaRightToLeft = false;  // PDFs and comics: right-to-left page order
    bool _grayPages = true;          // LILYGO: whole pages in 16 grays
    bool _grayZoom = false;          // LILYGO: zoomed views in 16 grays
    bool _soundEnabled = true;
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
    void loadDeviceSettings();
    void saveSleepSettings();
    void saveReaderSettings();
    void saveDisplayRotation();
    void showSettingsHome(const String& status = "");
    void showReaderDisplay(const String& status = "");
    void showSleepScreen(const String& status = "");
    void drawSettingsHome();
    void drawReaderDisplay();
    void drawSleepScreen();
    void drawLightButtons();
    void drawGamepad();
    void gamepadTouch(int row);
    std::vector<String> _padAddresses;   // found pads, in drawn row order
    String _padSignature;                // last drawn gamepad page state
    void drawNetworkList();
    void drawKeyboard();
    void drawConnectionResult();
};

#endif
