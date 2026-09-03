#include "AppWifi.h"

#if defined(BOARD_SEEED_STICKY)

#include "../../Book32_Core/AppMgr.h"
#include "../../Book32_Core/DisplayMgr.h"
#include "../../Book32_Core/FontMgr.h"
#include "../../Book32_Web/WebMgr.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <algorithm>

namespace {

constexpr int LIST_TOP = 165;
constexpr int LIST_ROW_HEIGHT = 72;
constexpr int NETWORKS_PER_PAGE = 7;
constexpr int KEYBOARD_TOP = 165;
constexpr int KEY_HEIGHT = 55;
constexpr int KEY_GAP = 5;
constexpr unsigned long CONNECT_TIMEOUT_MS = 20000;

String fittedText(FontMgr& fontMgr, const String& value, int width, int fontSize) {
    if (fontMgr.getTextWidth(value.c_str(), fontSize) <= width) return value;
    String clipped = value;
    while (clipped.length() > 1 &&
           fontMgr.getTextWidth((clipped + "...").c_str(), fontSize) > width) {
        clipped.remove(clipped.length() - 1);
    }
    return clipped + "...";
}

void drawCenteredButton(Book32Display& display, FontMgr& fontMgr, int x, int y,
                        int width, int height, const char* label, bool filled = false,
                        int fontSize = FONT_SIZE_SMALL) {
    if (filled) display.fillRect(x, y, width, height, GxEPD_BLACK);
    else display.drawRect(x, y, width, height, GxEPD_BLACK);
    uint16_t color = filled ? GxEPD_WHITE : GxEPD_BLACK;
    int textWidth = fontMgr.getTextWidth(label, fontSize);
    int baseline = y + (height / 2) + (fontMgr.getTextHeight(fontSize) / 3);
    fontMgr.drawText(display, label, x + (width - textWidth) / 2, baseline, fontSize, color);
}

void drawSignalBars(Book32Display& display, int x, int y, int32_t rssi) {
    int bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; ++i) {
        int height = 6 + (i * 6);
        if (i < bars) display.fillRect(x + (i * 8), y + 24 - height, 5, height, GxEPD_BLACK);
        else display.drawRect(x + (i * 8), y + 24 - height, 5, height, GxEPD_BLACK);
    }
}

void drawKeyRow(Book32Display& display, FontMgr& fontMgr, const char* keys,
                int y, bool uppercase) {
    int count = strlen(keys);
    if (count <= 0) return;
    int keyWidth = display.width() / count;
    char label[2] = {'\0', '\0'};
    for (int i = 0; i < count; ++i) {
        label[0] = uppercase && keys[i] >= 'a' && keys[i] <= 'z'
                       ? static_cast<char>(keys[i] - 'a' + 'A')
                       : keys[i];
        drawCenteredButton(display, fontMgr, i * keyWidth + 2, y,
                           keyWidth - 4, KEY_HEIGHT, label);
    }
}

char keyAt(const char* keys, uint16_t x, bool uppercase) {
    int count = strlen(keys);
    if (count <= 0) return 0;
    int index = min(count - 1, static_cast<int>(x) * count / SCREEN_WIDTH);
    char key = keys[index];
    if (uppercase && key >= 'a' && key <= 'z') key = static_cast<char>(key - 'a' + 'A');
    return key;
}

}  // namespace

void AppWifi::start() {
    _view = NETWORK_LIST;
    _page = 0;
    _status = WiFi.status() == WL_CONNECTED
                  ? String("Connected to ") + WiFi.SSID()
                  : "Choose a network";
    _connecting = false;
    _uppercase = false;
    _symbols = false;
    _showPassword = false;
    _needsRedraw = true;
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    InputMgr::getInstance().setCallback(std::bind(&AppWifi::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppWifi::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));
    startScan();
}

void AppWifi::stop() {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    _scanning = false;
    if (_connecting) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, true);
        _connecting = false;
    }
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppWifi::forceRedraw() {
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    _needsRedraw = true;
}

void AppWifi::returnToMenu() {
    AppMgr::getInstance().switchTo(0);
}

void AppWifi::handleInput(InputAction action) {
    if (action == INPUT_SELECT || action == INPUT_BACK) returnToMenu();
}

void AppWifi::startScan() {
    if (_connecting) return;
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    if (WiFi.status() != WL_CONNECTED) {
        // Stop a failed saved-credential loop before asking the radio to scan.
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, true);
        delay(200);
    }
    WiFi.mode(WIFI_STA);
    _networks.clear();
    _page = 0;
    _status = "Scanning for Wi-Fi...";
    _scanning = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;
    if (!_scanning) _status = "Could not start scan. Tap Rescan.";
    Serial.printf("Wi-Fi app scan %s\n", _scanning ? "started" : "failed to start");
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    _needsRedraw = true;
}

void AppWifi::finishScan(int count) {
    _networks.clear();
    for (int i = 0; i < count; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        int existing = -1;
        for (size_t j = 0; j < _networks.size(); ++j) {
            if (_networks[j].ssid == ssid) {
                existing = static_cast<int>(j);
                break;
            }
        }
        WifiNetworkEntry entry{ssid, WiFi.RSSI(i), static_cast<uint8_t>(WiFi.encryptionType(i))};
        if (existing < 0) _networks.push_back(entry);
        else if (entry.rssi > _networks[existing].rssi) _networks[existing] = entry;
    }
    std::sort(_networks.begin(), _networks.end(), [](const WifiNetworkEntry& a, const WifiNetworkEntry& b) {
        return a.rssi > b.rssi;
    });
    WiFi.scanDelete();
    _scanning = false;
    _status = _networks.empty() ? "No networks found" : "Tap a network to connect";
    Serial.printf("Wi-Fi app found %d unique networks\n", static_cast<int>(_networks.size()));
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    _needsRedraw = true;
}

void AppWifi::selectNetwork(int index) {
    if (index < 0 || index >= static_cast<int>(_networks.size())) return;
    _selectedSsid = _networks[index].ssid;
    _selectedAuth = _networks[index].auth;
    _password = "";
    _status = "Enter the Wi-Fi password";
    _uppercase = false;
    _symbols = false;
    _showPassword = false;
    if (_selectedAuth == WIFI_AUTH_OPEN) {
        beginConnect();
        return;
    }
    _view = PASSWORD_KEYBOARD;
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    _needsRedraw = true;
}

void AppWifi::beginConnect() {
    _scanning = false;
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(100);
    if (_selectedAuth == WIFI_AUTH_OPEN) WiFi.begin(_selectedSsid.c_str());
    else WiFi.begin(_selectedSsid.c_str(), _password.c_str());
    _connecting = true;
    _connectStarted = millis();
    _status = String("Connecting to ") + _selectedSsid + "...";
    Serial.printf("Wi-Fi app connecting to %s\n", _selectedSsid.c_str());
    _fullRefresh = true;
    _passwordOnlyRedraw = false;
    _needsRedraw = true;
}

void AppWifi::appendKey(char key) {
    if (!key || _password.length() >= 63 || _connecting) return;
    _password += key;
    if (_uppercase && key >= 'A' && key <= 'Z') {
        // Shift is one-shot, like a phone keyboard.
        _uppercase = false;
        _fullRefresh = true;
        _passwordOnlyRedraw = false;
    } else {
        _passwordOnlyRedraw = true;
    }
    _needsRedraw = true;
}

void AppWifi::handleTouch(uint16_t x, uint16_t y) {
    if (_view == NETWORK_LIST) {
        if (_connecting) return;
        if (y >= 55 && y < 112) {
            if (x < SCREEN_WIDTH / 2) returnToMenu();
            else startScan();
            return;
        }
        if (y >= LIST_TOP && y < LIST_TOP + NETWORKS_PER_PAGE * LIST_ROW_HEIGHT) {
            int row = (y - LIST_TOP) / LIST_ROW_HEIGHT;
            selectNetwork((_page * NETWORKS_PER_PAGE) + row);
            return;
        }
        if (y >= 700) {
            int pageCount = max(1, (static_cast<int>(_networks.size()) + NETWORKS_PER_PAGE - 1) / NETWORKS_PER_PAGE);
            if (x < SCREEN_WIDTH / 2 && _page > 0) --_page;
            else if (x >= SCREEN_WIDTH / 2 && _page + 1 < pageCount) ++_page;
            else return;
            _fullRefresh = false;
            _needsRedraw = true;
        }
        return;
    }

    if (_view == CONNECTION_RESULT) {
        if (y >= 640) returnToMenu();
        return;
    }

    if (y >= 88 && y < 145 && x >= 355) {
        _showPassword = !_showPassword;
        _fullRefresh = true;
        _passwordOnlyRedraw = false;
        _needsRedraw = true;
        return;
    }
    if (_connecting) return;

    const char* letterRows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const char* symbolRows[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}\\|", "`~;:'\",.<>/?"};
    for (int row = 0; row < 4; ++row) {
        int rowY = KEYBOARD_TOP + row * (KEY_HEIGHT + KEY_GAP);
        if (y >= rowY && y < rowY + KEY_HEIGHT) {
            const char* keys = _symbols ? symbolRows[row] : letterRows[row];
            appendKey(keyAt(keys, x, !_symbols && _uppercase));
            return;
        }
    }

    if (y >= 410 && y < 470) {
        if (x < 90) {
            _symbols = !_symbols;
            _fullRefresh = true;
            _passwordOnlyRedraw = false;
            _needsRedraw = true;
        } else if (x < 180) {
            if (!_symbols) _uppercase = !_uppercase;
            _fullRefresh = true;
            _passwordOnlyRedraw = false;
            _needsRedraw = true;
        } else if (x < 360) {
            appendKey(' ');
        } else if (_password.length() > 0) {
            _password.remove(_password.length() - 1);
            _fullRefresh = true;
            _passwordOnlyRedraw = false;
            _needsRedraw = true;
        }
        return;
    }

    if (y >= 490 && y < 555) {
        if (x < 150) {
            _view = NETWORK_LIST;
            _status = "Tap a network to connect";
            _fullRefresh = true;
            _passwordOnlyRedraw = false;
            _needsRedraw = true;
        } else if (x < 270) {
            _password = "";
            _fullRefresh = true;
            _passwordOnlyRedraw = false;
            _needsRedraw = true;
        } else {
            beginConnect();
        }
    }
}

void AppWifi::update() {
    unsigned long now = millis();
    if (now - _lastPoll < 250) return;
    _lastPoll = now;

    if (_scanning) {
        int result = WiFi.scanComplete();
        if (result >= 0) finishScan(result);
        else if (result == WIFI_SCAN_FAILED) {
            _scanning = false;
            _status = "Scan failed. Tap Rescan.";
            _fullRefresh = true;
            _needsRedraw = true;
        }
    }

    if (_connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            _connecting = false;
            _view = CONNECTION_RESULT;
            _status = String("Connected to ") + WiFi.SSID();
            WiFi.setAutoReconnect(true);
            WebMgr::getInstance().init();
            Serial.printf("Wi-Fi app connected to %s at %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            _fullRefresh = true;
            _needsRedraw = true;
        } else if (now - _connectStarted >= CONNECT_TIMEOUT_MS) {
            _connecting = false;
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            _status = "Could not connect. Check the password.";
            Serial.println("Wi-Fi app connection timed out; failed credentials cleared");
            _fullRefresh = true;
            _needsRedraw = true;
        }
    }
}

void AppWifi::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    if (_view == NETWORK_LIST) drawNetworkList();
    else if (_view == PASSWORD_KEYBOARD) drawKeyboard();
    else drawConnectionResult();
}

void AppWifi::drawNetworkList() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    if (_fullRefresh) display.setFullWindow();
    else display.setPartialWindow(0, 0, display.width(), display.height());
    _fullRefresh = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        fontMgr.drawText(display, "Wi-Fi Setup", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(0, 50, display.width(), 50, GxEPD_BLACK);
        drawCenteredButton(display, fontMgr, 10, 58, 220, 52, "< Back");
        drawCenteredButton(display, fontMgr, 250, 58, 220, 52, _scanning ? "Scanning..." : "Rescan");
        String status = fittedText(fontMgr, _status, display.width() - 30, FONT_SIZE_SMALL);
        fontMgr.drawTextCentered(display, status.c_str(), 145, FONT_SIZE_SMALL, GxEPD_BLACK);

        int start = _page * NETWORKS_PER_PAGE;
        int end = min(static_cast<int>(_networks.size()), start + NETWORKS_PER_PAGE);
        for (int i = start; i < end; ++i) {
            int row = i - start;
            int y = LIST_TOP + row * LIST_ROW_HEIGHT;
            display.drawRect(10, y + 3, display.width() - 20, LIST_ROW_HEIGHT - 7, GxEPD_BLACK);
            String ssid = fittedText(fontMgr, _networks[i].ssid, display.width() - 125, FONT_SIZE_BODY);
            fontMgr.drawText(display, ssid.c_str(), 24, y + 34, FONT_SIZE_BODY, GxEPD_BLACK);
            drawSignalBars(display, display.width() - 90, y + 20, _networks[i].rssi);
            if (_networks[i].auth != WIFI_AUTH_OPEN) {
                display.drawRect(display.width() - 43, y + 22, 18, 17, GxEPD_BLACK);
                display.drawCircle(display.width() - 34, y + 21, 7, GxEPD_BLACK);
                display.fillRect(display.width() - 42, y + 21, 17, 8, GxEPD_WHITE);
                display.drawRect(display.width() - 43, y + 22, 18, 17, GxEPD_BLACK);
            }
            if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == _networks[i].ssid) {
                fontMgr.drawText(display, "Connected", 24, y + 57, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
        }

        int pageCount = max(1, (static_cast<int>(_networks.size()) + NETWORKS_PER_PAGE - 1) / NETWORKS_PER_PAGE);
        if (pageCount > 1) {
            drawCenteredButton(display, fontMgr, 10, 720, 140, 55, "Previous", _page > 0);
            char pageLabel[24];
            snprintf(pageLabel, sizeof(pageLabel), "%d / %d", _page + 1, pageCount);
            fontMgr.drawTextCentered(display, pageLabel, 754, FONT_SIZE_SMALL, GxEPD_BLACK);
            drawCenteredButton(display, fontMgr, 330, 720, 140, 55, "Next", _page + 1 < pageCount);
        }
    } while (display.nextPage());
}

void AppWifi::drawKeyboard() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    if (_fullRefresh) display.setFullWindow();
    else if (_passwordOnlyRedraw) display.setPartialWindow(0, 75, display.width(), 75);
    else display.setPartialWindow(0, 0, display.width(), display.height());
    _fullRefresh = false;
    _passwordOnlyRedraw = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        String title = fittedText(fontMgr, String("Join ") + _selectedSsid,
                                  display.width() - 30, FONT_SIZE_SUBTITLE);
        fontMgr.drawText(display, title.c_str(), 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(0, 50, display.width(), 50, GxEPD_BLACK);

        display.drawRect(10, 88, 335, 57, GxEPD_BLACK);
        String shown;
        if (_showPassword) shown = _password;
        else for (size_t i = 0; i < _password.length(); ++i) shown += '*';
        shown = fittedText(fontMgr, shown, 310, FONT_SIZE_BODY);
        fontMgr.drawText(display, shown.c_str(), 22, 124, FONT_SIZE_BODY, GxEPD_BLACK);
        drawCenteredButton(display, fontMgr, 355, 88, 115, 57,
                           _showPassword ? "Hide" : "Show");

        const char* letterRows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
        const char* symbolRows[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}\\|", "`~;:'\",.<>/?"};
        for (int row = 0; row < 4; ++row) {
            drawKeyRow(display, fontMgr, _symbols ? symbolRows[row] : letterRows[row],
                       KEYBOARD_TOP + row * (KEY_HEIGHT + KEY_GAP), !_symbols && _uppercase);
        }

        drawCenteredButton(display, fontMgr, 2, 410, 86, 60, _symbols ? "abc" : "#+=");
        drawCenteredButton(display, fontMgr, 92, 410, 86, 60,
                           _symbols ? "" : (_uppercase ? "lower" : "Shift"));
        drawCenteredButton(display, fontMgr, 182, 410, 176, 60, "Space");
        drawCenteredButton(display, fontMgr, 362, 410, 116, 60, "Delete");
        drawCenteredButton(display, fontMgr, 10, 490, 130, 65, "Cancel");
        drawCenteredButton(display, fontMgr, 150, 490, 110, 65, "Clear");
        drawCenteredButton(display, fontMgr, 270, 490, 200, 65,
                           _connecting ? "Connecting..." : "Connect", true);
        String status = fittedText(fontMgr, _status, display.width() - 30, FONT_SIZE_BODY);
        fontMgr.drawTextCentered(display, status.c_str(), 605, FONT_SIZE_BODY, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "Power button: back to menu", 770,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);
    } while (display.nextPage());
}

void AppWifi::drawConnectionResult() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    if (_fullRefresh) display.setFullWindow();
    else display.setPartialWindow(0, 0, display.width(), display.height());
    _fullRefresh = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        fontMgr.drawTextCentered(display, "Wi-Fi Connected", 190, FONT_SIZE_TITLE, GxEPD_BLACK);
        String ssid = fittedText(fontMgr, WiFi.SSID(), display.width() - 40, FONT_SIZE_SUBTITLE);
        fontMgr.drawTextCentered(display, ssid.c_str(), 270, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, WiFi.localIP().toString().c_str(), 325,
                                 FONT_SIZE_BODY, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "Password saved for future starts", 390,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);
        drawCenteredButton(display, fontMgr, 70, 640, 340, 75, "Back to Menu", true,
                           FONT_SIZE_BODY);
    } while (display.nextPage());
}

#endif
