#include "DisplayMgr.h"
#include "../../include/Config.h"
#include "Book32FS.h"
#include "BatteryMgr.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

static void drawCenteredText(Book32Display& display, const char* text, const GFXfont* font, int16_t baseline, uint16_t color) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(font);
    display.setTextColor(color);
    display.setTextSize(1);
    display.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, baseline);
    display.print(text);
}

// Boot screen geometry: the 800 px tall portrait screens, and a compact
// version for the RLCD's 400 px.
struct BootLayout {
    int16_t inset, rule, logoY, titleY, nameDY, barW, barH, barY, percentDY, statusDY, clearH, versionUp;
};
static const BootLayout& bootLayout(int16_t screenH) {
    static const BootLayout tall = {18, 64, 185, 330, 42, 320, 28, 430, 76, 116, 150, 96};
    static const BootLayout compact = {10, 40, 70, 190, 28, 240, 24, 245, 60, 95, 110, 18};
    return screenH < 600 ? compact : tall;
}

static void drawBootProgress(Book32Display& display, uint8_t progress, const char* status) {
    int16_t screenW = display.width();
    const BootLayout& layout = bootLayout(display.height());
    const int16_t barW = layout.barW;
    const int16_t barH = layout.barH;
    const int16_t barX = (screenW - barW) / 2;
    const int16_t barY = layout.barY;
    const int16_t fillW = ((barW - 8) * progress) / 100;

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", (unsigned int)progress);

    display.fillRect(barX - 20, barY - 8, barW + 40, layout.clearH, GxEPD_WHITE);
    display.drawRoundRect(barX, barY, barW, barH, 7, GxEPD_BLACK);
    display.drawRoundRect(barX + 2, barY + 2, barW - 4, barH - 4, 5, GxEPD_BLACK);
    if (fillW > 0) {
        display.fillRoundRect(barX + 4, barY + 4, fillW, barH - 8, 3, GxEPD_BLACK);
    }

    drawCenteredText(display, percentText, &FreeSansBold18pt7b, barY + layout.percentDY, GxEPD_BLACK);
    drawCenteredText(display, status, &FreeSans12pt7b, barY + layout.statusDY, GxEPD_BLACK);
}

// The orientation is mirrored into NVS so the very first boot frame is drawn
// the right way up; /display_config.json (on ebook storage) stays the source
// of truth and is re-applied by loadDisplaySettings() once storage mounts.
#if BOOK32_GXEPD_PANEL
static const unsigned long PANEL_POWER_HOLD_MS = 10000;
#endif

static const char* ROTATION_PREFS_NAMESPACE = "display";
static const char* ROTATION_PREFS_KEY = "rotation";

static int loadCachedRotation() {
    Preferences prefs;
    int rotation = 3;
    if (prefs.begin(ROTATION_PREFS_NAMESPACE, true)) {
        rotation = prefs.getInt(ROTATION_PREFS_KEY, 3);
        prefs.end();
    }
    return (rotation == 1 || rotation == 3) ? rotation : 3;
}

static void saveCachedRotation(int rotation) {
    Preferences prefs;
    if (prefs.begin(ROTATION_PREFS_NAMESPACE, false)) {
        prefs.putInt(ROTATION_PREFS_KEY, rotation);
        prefs.end();
    }
}

// Constructor with Pin mapping
// GxEPD2_420(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
#if BOOK32_GXEPD_PANEL
DisplayMgr::DisplayMgr() : display(GxEPD2_750_T7_Fast(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}
#else
DisplayMgr::DisplayMgr() : display() {}
#endif

DisplayMgr& DisplayMgr::getInstance() {
    static DisplayMgr instance;
    return instance;
}

void DisplayMgr::init() {
    // For ESP32-S3 we must initialize SPI with custom pins before display.init
#if !defined(BOARD_LILYGO_T5S3_PRO) && !defined(BOARD_WAVESHARE_RLCD42)
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
#endif
#if BOOK32_GXEPD_PANEL
    // GxEPD2 defaults to 4MHz and sends 48KB frames byte by byte. The UC8179
    // controller accepts writes well above 10MHz; stay conservative for the
    // DIY kit's wiring.
    display.epd2.selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
#endif

    _rotation = loadCachedRotation();
    display.init(115200, true, 10, false);

    display.setRotation(_rotation); // Portrait mode (480x800); _rotation = 1 or 3
    display.setTextColor(GxEPD_BLACK);
    display.setFont(NULL);

    Serial.printf("Display initialized: %dx%d (rotation %d)\n", display.width(), display.height(), _rotation);
}

void DisplayMgr::setRotation(int rotation) {
    // Constrain to the two portrait orientations so layouts never break.
    if (rotation != 1 && rotation != 3) rotation = 3;
    bool changed = (rotation != _rotation);
#if BOOK32_ASYNC_BOOT_SCREEN
    // Never rotate the canvas underneath a boot frame that is being drawn.
    if (changed) waitForBootScreenIdle();
#endif
    _rotation = rotation;
    if (changed) saveCachedRotation(rotation);
    display.setRotation(_rotation);
    // Only invalidate the boot screen when the orientation actually changes, so
    // a no-op load at boot doesn't trigger a full refresh mid-sequence. Live
    // (post-boot) rotation changes repaint via the active app's forceRedraw().
    if (changed) _bootScreenActive = false;
    Serial.printf("Display rotation set to %d (changed=%d)\n", _rotation, changed);
}

bool DisplayMgr::mapNativeTouchToScreen(uint16_t nativeX, uint16_t nativeY,
                                        uint16_t& screenX, uint16_t& screenY) const {
#if defined(BOARD_LILYGO_T5S3_PRO)
    return LilygoLayout::mapTouch(nativeX, nativeY, _rotation, screenX, screenY);
#else
    // Native panel coordinates are 800x480. These transformations mirror
    // Adafruit_GFX's rotation mapping into Book32's 480x800 portrait space.
    if (nativeX >= PANEL_WIDTH || nativeY >= PANEL_HEIGHT) return false;
    if (_rotation == 1) {
        screenX = nativeY;
        screenY = PANEL_WIDTH - 1 - nativeX;
    } else {
        screenX = PANEL_HEIGHT - 1 - nativeY;
        screenY = nativeX;
    }
    return screenX < SCREEN_WIDTH && screenY < SCREEN_HEIGHT;
#endif
}

void DisplayMgr::loadDisplaySettings() {
    int rotation = 3;  // Default: buttons on the right
    if (EbookFS.exists("/display_config.json")) {
        File file = EbookFS.open("/display_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(256);
            if (!deserializeJson(doc, file)) {
                rotation = doc["rotation"] | 3;
            }
            file.close();
        }
    }
    setRotation(rotation);
    Serial.printf("Loaded display settings: rotation=%d\n", _rotation);
}

void DisplayMgr::clear() {
    display.clearScreen();
}

void DisplayMgr::fullRefresh() {
    display.refresh(false); // Full update
}

void DisplayMgr::showBootScreen(uint8_t progress, const char* status) {
    if (progress > 100) progress = 100;
    if (!status) status = "";

#if BOOK32_ASYNC_BOOT_SCREEN
    if (!_bootFinished) {
        if (!_bootLock) _bootLock = xSemaphoreCreateMutex();
        if (_bootLock) {
            xSemaphoreTake(_bootLock, portMAX_DELAY);
            _bootProgress = progress;
            strlcpy(_bootStatus, status, sizeof(_bootStatus));
            _bootPending = true;
            bool startWorker = !_bootTaskRunning;
            if (startWorker) _bootTaskRunning = true;
            xSemaphoreGive(_bootLock);

            if (!startWorker) return;
            if (xTaskCreatePinnedToCore(bootScreenTask, "BootScreen", 6144, this, 1, nullptr, 1) == pdPASS) {
                return;
            }
            // No worker: fall through and draw this status synchronously.
            xSemaphoreTake(_bootLock, portMAX_DELAY);
            _bootTaskRunning = false;
            _bootPending = false;
            xSemaphoreGive(_bootLock);
        }
    }
#endif
    renderBootScreen(progress, status);
}

#if BOOK32_ASYNC_BOOT_SCREEN
void DisplayMgr::bootScreenTask(void* parameter) {
    DisplayMgr* self = static_cast<DisplayMgr*>(parameter);
    for (;;) {
        xSemaphoreTake(self->_bootLock, portMAX_DELAY);
        if (!self->_bootPending) {
            self->_bootTaskRunning = false;
            xSemaphoreGive(self->_bootLock);
            break;
        }
        uint8_t progress = self->_bootProgress;
        char status[sizeof(self->_bootStatus)];
        strlcpy(status, self->_bootStatus, sizeof(status));
        self->_bootPending = false;
        xSemaphoreGive(self->_bootLock);

        self->renderBootScreen(progress, status);
    }
    vTaskDelete(nullptr);
}

void DisplayMgr::waitForBootScreenIdle() {
    while (_bootTaskRunning) delay(5);
}
#endif

void DisplayMgr::finishBootScreen() {
#if BOOK32_ASYNC_BOOT_SCREEN
    waitForBootScreenIdle();
    _bootFinished = true;
#endif
}

void DisplayMgr::renderBootScreen(uint8_t progress, const char* status) {
    int16_t screenW = display.width();
    int16_t screenH = display.height();
    const BootLayout& layout = bootLayout(screenH);

    bool drawFullFrame = !_bootScreenActive;
    if (drawFullFrame) {
        display.setFullWindow();
        _bootScreenActive = true;
    } else {
        const int16_t barX = (screenW - layout.barW) / 2;
        display.setPartialWindow(barX - 20, layout.barY - 20, layout.barW + 40, layout.clearH + 30);
    }

    const int16_t frameInset = layout.inset;
    const int16_t logoY = layout.logoY;
    const int16_t titleY = layout.titleY;

    display.firstPage();
    do {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);

        if (drawFullFrame) {
            display.fillScreen(GxEPD_WHITE);
            display.drawRect(frameInset, frameInset, screenW - (frameInset * 2), screenH - (frameInset * 2), GxEPD_BLACK);
            display.drawFastHLine(54, layout.rule, screenW - 108, GxEPD_BLACK);
            display.drawFastHLine(54, screenH - layout.rule, screenW - 108, GxEPD_BLACK);

            int16_t logoX = (screenW - 116) / 2;
            display.drawRoundRect(logoX, logoY, 54, 70, 5, GxEPD_BLACK);
            display.drawRoundRect(logoX + 62, logoY, 54, 70, 5, GxEPD_BLACK);
            display.fillRect(logoX + 55, logoY + 6, 6, 58, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 18, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 32, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 46, 28, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 18, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 32, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 46, 28, GxEPD_BLACK);

            drawCenteredText(display, "InkDeck", &FreeSansBold24pt7b, titleY, GxEPD_BLACK);
            drawCenteredText(display, DEVICE_NAME, &FreeSans12pt7b, titleY + layout.nameDY, GxEPD_BLACK);

            char versionText[20];
            snprintf(versionText, sizeof(versionText), "v%s", SYSTEM_VERSION);
            drawCenteredText(display, versionText, &FreeSans9pt7b, screenH - layout.versionUp, GxEPD_BLACK);
        }

        drawBootProgress(display, progress, status);
    } while (display.nextPage());
}

void DisplayMgr::update() {
#if BOOK32_GXEPD_PANEL
    // GxEPD2 only powers the 7.5" panel down after a full refresh; partial
    // updates leave its high-voltage booster on indefinitely, which slowly
    // darkens the panel edge. Keep it armed briefly after a button press so
    // quick page turns skip the ~150ms re-arm, then power it down.
    // powerOff() is a no-op when already off.
    if (millis() - BatteryMgr::getInstance().getLastActivityTime() >= PANEL_POWER_HOLD_MS) {
        display.powerOff();
    }
#endif
}
