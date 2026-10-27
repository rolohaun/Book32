#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "NetworkState.h"
#if BOOK32_HAS_INKBOY
#include "NetworkWork.h"
#endif

#include "DisplayMgr.h"
#include "InputMgr.h"
#include "GamepadMgr.h"
#include "AppMgr.h"
#include "WebMgr.h"
#include "GitHubMgr.h"
#include "BatteryMgr.h"
#include "FontMgr.h"
#include "SoundMgr.h"
#if BOOK32_HAS_INKBOY
#include "../Apps/AppPaperboy/AppPaperboy.h"
#endif

#include "../Book32_Apps/AppMainMenu.h"
#include "../Apps/AppReader/AppReader.h"
#include "../Apps/AppKlipper/AppKlipper.h"
#include "../Apps/AppTodo/AppTodo.h"
#if BOOK32_HAS_TOUCH
#include "../Apps/AppWifi/AppWifi.h"
#endif
#include <WiFiManager.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

volatile bool gNetworkStartupInProgress = false;
#if !BOOK32_HAS_TOUCH
static WiFiManager* gWifiManager = nullptr;
#endif

static void networkStartupWork(void* parameter) {
    (void)parameter;

    Serial.println("Network startup task started");
#if BOOK32_HAS_TOUCH
    // Sticky performs all first-time setup on its own touch screen. At boot,
    // only try credentials already saved by the Wi-Fi app.
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
#if BOOK32_HAS_INKBOY
        if (NetworkWork::gate().paused()) {
            gNetworkStartupInProgress=false;
            return;
        }
#endif
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current && (strcmp(current->getName(), "Settings") == 0 || strcmp(current->getName(), "Inkade") == 0)) {
            // The interactive app now owns the radio and scan lifecycle.
            gNetworkStartupInProgress = false;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        ++attempts;
    }
    bool connected = WiFi.status() == WL_CONNECTED;
#else
    if (!gWifiManager) gWifiManager = new WiFiManager();
    // Don't let the setup portal block forever when no known network is in
    // range. On timeout autoConnect returns false and the main menu brings up
    // the InkDeck management hotspot instead.
    gWifiManager->setConfigPortalTimeout(120);
    // Without this, an out-of-range saved network waits on the ESP's default
    // 60s connect timeout before the portal opens. Match the touch boards'
    // ~10s saved-network attempt with a little headroom.
    gWifiManager->setConnectTimeout(15);
    bool connected = gWifiManager->autoConnect("InkDeck-Setup");
#endif

    if (!connected) {
#if BOOK32_HAS_TOUCH
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
#endif
        Serial.println("WiFi setup did not connect; continuing offline");
        gNetworkStartupInProgress = false;
        return;
    }

    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());

    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (currentApp && (strcmp(currentApp->getName(), "eReader") == 0 || strcmp(currentApp->getName(), "Inkade") == 0)) {
        Serial.println("Network startup skipped services; eReader is active");
        if (strcmp(currentApp->getName(),"Inkade")==0) {
            gNetworkStartupInProgress=false;
            return; // Inkade is the sole radio-shutdown owner.
        }
#if BOOK32_HAS_INKBOY
        // Inkade owns shutdown and waits for this lease plus active HTTPS.
        if (NetworkWork::gate().paused()) { gNetworkStartupInProgress=false; return; }
#endif
        WebMgr::getInstance().stop();
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        gNetworkStartupInProgress = false;
        return;
    }

    // WiFiManager releases its config portal before returning, but a short yield
    // gives the networking stack a clean handoff before starting our server.
    vTaskDelay(pdMS_TO_TICKS(250));

    WebMgr::getInstance().init();

    Serial.println("Network services ready");
    gNetworkStartupInProgress = false;
}

static void networkStartupTask(void* parameter) {
#if BOOK32_HAS_INKBOY
    {
        NetworkWork::Lease network;
        if (network) networkStartupWork(parameter);
        else gNetworkStartupInProgress=false;
    }
#else
    networkStartupWork(parameter);
#endif
    vTaskDelete(nullptr);
}

void setup() {
#if defined(BOARD_WAVESHARE_RLCD42)
    // The LCD's control pins are held through deep sleep so it keeps its
    // picture. Release them, and if KEY woke us, wait for that press to end
    // so it is not also taken as "next".
    {
        const bool wokeFromButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(static_cast<gpio_num_t>(RLCD_CS));
        gpio_hold_dis(static_cast<gpio_num_t>(RLCD_RST));
        gpio_hold_dis(static_cast<gpio_num_t>(AUDIO_PA));
        if (wokeFromButton) {
            pinMode(PIN_BUTTON, INPUT_PULLUP);
            while (digitalRead(PIN_BUTTON) == LOW) delay(20);
            delay(50);
        }
    }
#elif !BOOK32_HAS_TOUCH
    // Book32 holds its battery-divider switch and display control pins
    // through deep sleep. Release them, and if the button woke us, wait for
    // that press to end so it is not also taken as "next".
    {
        const bool wokeFromButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
        gpio_deep_sleep_hold_dis();
#ifdef PIN_VBAT_SWITCH
        gpio_hold_dis(static_cast<gpio_num_t>(PIN_VBAT_SWITCH));
#endif
        gpio_hold_dis(static_cast<gpio_num_t>(EPD_CS));
        gpio_hold_dis(static_cast<gpio_num_t>(EPD_RST));
        if (wokeFromButton) {
            pinMode(PIN_BUTTON, INPUT_PULLUP);
            while (digitalRead(PIN_BUTTON) == LOW) delay(20);
            delay(50);
        }
    }
#endif
#if defined(BOARD_SEEED_STICKY)
    const bool wokeFromPowerButton =
        esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;

    // Deep-sleep GPIO holds survive the reset which follows wake-up. Release
    // them before asserting the Sticky's power latches and peripheral rails.
    gpio_deep_sleep_hold_dis();
    for (int pin : {PIN_POWER_HOLD, PIN_POWER_LOCK, PIN_CHARGE_ENABLE,
                    EPD_RST, EPD_ENABLE, TOUCH_RST, TOUCH_ENABLE,
                    SD_POWER_ENABLE, PIN_MIC_ENABLE, PIN_BUZZER}) {
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
    }

    // Sticky's power-hold rails must be asserted before any slow startup work.
    pinMode(PIN_POWER_HOLD, OUTPUT);
    digitalWrite(PIN_POWER_HOLD, HIGH);
    pinMode(PIN_POWER_LOCK, OUTPUT);
    digitalWrite(PIN_POWER_LOCK, HIGH);
    pinMode(PIN_CHARGE_ENABLE, OUTPUT);
    digitalWrite(PIN_CHARGE_ENABLE, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(PIN_CHARGE_ENABLE));

    // The SD card shares the display SPI bus. Restore its rail before the first
    // display command so a previously unpowered card cannot clamp SCLK/MOSI.
    pinMode(SD_POWER_ENABLE, OUTPUT);
    digitalWrite(SD_POWER_ENABLE, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // A wake press can still be held while the ESP restarts. Waiting here keeps
    // it from being interpreted as another two-second sleep request.
    if (wokeFromPowerButton) {
        pinMode(PIN_BUTTON, INPUT_PULLUP);
        while (digitalRead(PIN_BUTTON) == LOW) delay(20);
        delay(50);
    }
#endif

    Serial.begin(115200);
    delay(250);
#if defined(BOARD_SEEED_STICKY)
    if (wokeFromPowerButton) Serial.println("Woke from deep sleep by power button");
#endif

    // Bring the E-ink panel up before the slower startup work begins.
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.showBootScreen(8, "Display ready");

    Serial.println("\n\n");
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║          InkDeck Starting...          ║");
    Serial.printf( "║  Build: %s %s  ║\n", __DATE__, __TIME__);
    Serial.println("╚═══════════════════════════════════════╝");

    // Get singleton instances (must be done after Arduino init, not at global scope)
    InputMgr& inputMgr = InputMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    WebMgr& webMgr = WebMgr::getInstance();
    GitHubMgr& gitHubMgr = GitHubMgr::getInstance();

    // 2. Mount Filesystems EARLY (before WiFi, prevents race conditions)
    displayMgr.showBootScreen(28, "Mounting storage");
    webMgr.mountFilesystems();

    // Touch feedback is persisted in ebook storage and is available to every
    // app. Boards without a buzzer simply expose a no-op SoundMgr.
    SoundMgr::getInstance().init();
    
    // 2.5. Initialize Font Manager (after filesystems, before UI)
    FontMgr::getInstance().init();

    // Apply the saved display orientation now that the filesystem is mounted
    // (the boot screen briefly showed in the default orientation before this).
    displayMgr.loadDisplaySettings();

    // 3. Battery/Input/App Init. Network services start in the background so
    // the menu is usable while WiFi and the web server finish coming up.
    displayMgr.showBootScreen(72, "Preparing controls");
    BatteryMgr::getInstance().init();

    // 4. Input Init
    inputMgr.init();
    // Bluetooth LE gamepad: reconnects a saved pad in the background.
    GamepadMgr::getInstance().begin();

    // 5. App Init
    appMgr.registerApp(new AppMainMenu());
    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);
    BatteryMgr::getInstance().setSleepCoverRenderer(AppReader::drawSleepCover);
    appMgr.registerApp(new AppTodo());
    appMgr.registerApp(new AppKlipper());
#if BOOK32_HAS_TOUCH
    appMgr.registerApp(new AppWifi());
#endif
#if BOOK32_HAS_INKBOY
    appMgr.registerApp(new AppPaperboy());
#endif

    displayMgr.showBootScreen(90, "Starting network");
    gNetworkStartupInProgress = true;
    BaseType_t networkTaskStarted = xTaskCreatePinnedToCore(
        networkStartupTask,
        "NetworkStart",
        12288,
        nullptr,
        1,
        nullptr,
        0
    );
    if (networkTaskStarted != pdPASS) {
        gNetworkStartupInProgress = false;
        Serial.println("Failed to start network task; continuing offline");
    }

    if (readerApp->hasBootResume()) {
        displayMgr.showBootScreen(100, "Opening reader");
        displayMgr.finishBootScreen();
        readerApp->resumeSavedBookOnStart();
        appMgr.switchTo(1);
    } else {
        displayMgr.showBootScreen(100, "Opening menu");
        displayMgr.finishBootScreen();
        appMgr.switchTo(0);
    }

    Serial.println("Setup Complete");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();
    AppMgr::getInstance().draw();  // Trigger app rendering
    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();  // Check charging state and critical battery
    BatteryMgr::getInstance().drawStatusIndicator();  // Update charging indicator on e-ink (partial)
    DisplayMgr::getInstance().update();  // Power the panel down between updates
}
