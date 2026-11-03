#include "HomeServices.h"
#include "AppMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>
#if BOOK32_HAS_INKBOY
#include "NetworkWork.h"
#endif

HomeServices& HomeServices::getInstance() {
    static HomeServices instance;
    return instance;
}

bool HomeServices::homeShowing() const {
    App* current = AppMgr::getInstance().getCurrentApp();
    if (!current) return false;
    if (strcmp(current->getName(), "Main Menu") == 0) return true;
    return _libraryHome && strcmp(current->getName(), "eReader") == 0;
}

bool HomeServices::offlineAppActive() const {
    if (homeShowing()) return false;
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && (strcmp(current->getName(), "eReader") == 0 || strcmp(current->getName(), "Inkade") == 0);
}

void HomeServices::updateCheckTask(void* parameter) {
    HomeServices* self = static_cast<HomeServices*>(parameter);

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
        self->_updateVersion = info.available ? info.version : String();
        if (info.available) self->_changed = true;  // Repaint to show the notice
    }

    self->_updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void HomeServices::wifiWakeTask(void* parameter) {
#if BOOK32_HAS_INKBOY
    {
        NetworkWork::Lease network;
        if (network) wifiWakeWork(parameter);
        else {
            auto* self = static_cast<HomeServices*>(parameter);
            self->_wifiStarting = false;
            self->_wifiTaskHandle = nullptr;
        }
    } // Destroy the lease BEFORE vTaskDelete (which does not unwind C++).
#else
    wifiWakeWork(parameter);
#endif
    vTaskDelete(nullptr);
}

void HomeServices::wifiWakeWork(void* parameter) {
    HomeServices* self = static_cast<HomeServices*>(parameter);
    Serial.println("Home WiFi wake task started");

    if (self->offlineAppActive()) {
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
            self->_wifiStarting = false;
            self->_wifiTaskHandle = nullptr;
            return; // The offline app shuts down radio after all owners drain.
        }
#endif
#if BOOK32_HAS_TOUCH
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current && strcmp(current->getName(), "Settings") == 0) {
            self->_wifiStarting = false;
            self->_wifiTaskHandle = nullptr;
            Serial.println("Home WiFi wake handed radio to Settings app");
            return;
        }
#endif
        if (self->offlineAppActive()) {
#if BOOK32_HAS_INKBOY
            App* owner = AppMgr::getInstance().getCurrentApp();
            if (owner && strcmp(owner->getName(), "Inkade") == 0) {
                self->_wifiStarting = false; self->_wifiTaskHandle = nullptr;
                return; // Inkade alone drains HTTPS then powers down radio.
            }
#endif
            WebMgr::getInstance().stop();
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
            self->_wifiStarting = false;
            self->_changed = true;
            self->_wifiTaskHandle = nullptr;
            Serial.println("Home WiFi wake cancelled; a book or game is open");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED && !self->offlineAppActive()) {
        Serial.println("Home WiFi connected");
        Serial.println(WiFi.localIP());
        WebMgr::getInstance().init();
    } else {
        Serial.println("Home WiFi wake did not connect");
        self->_wifiTaskHandle = nullptr;  // Clear before starting the hotspot
#if !BOOK32_HAS_TOUCH
        if (!self->offlineAppActive()) self->startHotspot();
#else
        Serial.println("Offline; use Wi-Fi inside the on-device Settings app");
#endif
        self->_wifiStarting = false;
        self->_changed = true;
        return;
    }

    self->_wifiStarting = false;
    self->_changed = true;
    self->_wifiTaskHandle = nullptr;
}

String HomeServices::footerText() const {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != INADDR_NONE) return ip.toString();
    }
    if (_hotspotActive) return String("Wi-Fi: ") + AP_SSID + "  ->  192.168.4.1";
    return _wifiStarting ? "WiFi starting" : "WiFi offline";
}

void HomeServices::startHotspot() {
    if (_hotspotActive || offlineAppActive()) return;

    Serial.println("Home: starting InkDeck management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);  // AP serves the web UI; STA stays available for joining a network
    WiFi.softAP(AP_SSID);
    delay(100);  // Let the AP interface come up before binding the server
    WebMgr::getInstance().init();
    _hotspotActive = true;
    _changed = true;

    Serial.print("Hotspot ready at ");
    Serial.println(WiFi.softAPIP());
}

void HomeServices::stopHotspot() {
    if (!_hotspotActive) return;

    Serial.println("Home: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    // Drop back to station-only; preserves an active connection if one exists.
    WiFi.mode(WIFI_STA);
    _hotspotActive = false;
}

void HomeServices::ensureWifiAwake() {
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

void HomeServices::enter() {
    _lastFooter = footerText();
    ensureWifiAwake();
    if (!_updateTaskHandle && !_updateAvailable) {
        xTaskCreatePinnedToCore(updateCheckTask, "UpdateCheck", 8192, this, 1, &_updateTaskHandle, 0);
    }
}

void HomeServices::leave() {
    // The hotspot is a home-screen convenience: other apps should not keep the
    // radio (and battery) busy. Station connections stay (Klipper needs Wi-Fi).
    stopHotspot();
}

bool HomeServices::poll() {
    const unsigned long now = millis();
    if (now - _lastNetworkPoll >= 1000) {
        _lastNetworkPoll = now;
        if (WiFi.status() == WL_CONNECTED) {
            _wifiStarting = false;
        } else if (!gNetworkStartupInProgress && !_wifiTaskHandle) {
            _wifiStarting = false;
            // Offline and idle: bring up the management hotspot so a phone can
            // still reach the web interface without a router.
#if !BOOK32_HAS_TOUCH
            if (!_hotspotActive && !offlineAppActive()) startHotspot();
#endif
        }
        const String footer = footerText();
        if (footer != _lastFooter) {
            _lastFooter = footer;
            _changed = true;
        }
    }
    return _changed.exchange(false);
}
