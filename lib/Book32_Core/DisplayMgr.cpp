#include "DisplayMgr.h"
#include "../../include/Config.h"
#include "Book32FS.h"
#include <ArduinoJson.h>
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

static void drawBootProgress(Book32Display& display, uint8_t progress, const char* status) {
    int16_t screenW = display.width();
    const int16_t barW = 320;
    const int16_t barH = 28;
    const int16_t barX = (screenW - barW) / 2;
    const int16_t barY = 430;
    const int16_t fillW = ((barW - 8) * progress) / 100;

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", (unsigned int)progress);

    display.fillRect(barX - 20, barY - 8, barW + 40, 150, GxEPD_WHITE);
    display.drawRoundRect(barX, barY, barW, barH, 7, GxEPD_BLACK);
    display.drawRoundRect(barX + 2, barY + 2, barW - 4, barH - 4, 5, GxEPD_BLACK);
    if (fillW > 0) {
        display.fillRoundRect(barX + 4, barY + 4, fillW, barH - 8, 3, GxEPD_BLACK);
    }

    drawCenteredText(display, percentText, &FreeSansBold18pt7b, barY + 76, GxEPD_BLACK);
    drawCenteredText(display, status, &FreeSans12pt7b, barY + 116, GxEPD_BLACK);
}

// Constructor with Pin mapping
// GxEPD2_420(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
#if defined(BOARD_SEEED_STICKY)
DisplayMgr::DisplayMgr() : display() {}
#else
DisplayMgr::DisplayMgr() : display(GxEPD2_750_T7(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}
#endif

DisplayMgr& DisplayMgr::getInstance() {
    static DisplayMgr instance;
    return instance;
}

void DisplayMgr::init() {
    // For ESP32-S3 we must initialize SPI with custom pins before display.init
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS); 
    
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
    _rotation = rotation;
    display.setRotation(_rotation);
    // Only invalidate the boot screen when the orientation actually changes, so
    // a no-op load at boot doesn't trigger a full refresh mid-sequence. Live
    // (post-boot) rotation changes repaint via the active app's forceRedraw().
    if (changed) _bootScreenActive = false;
    Serial.printf("Display rotation set to %d (changed=%d)\n", _rotation, changed);
}

bool DisplayMgr::mapNativeTouchToScreen(uint16_t nativeX, uint16_t nativeY,
                                        uint16_t& screenX, uint16_t& screenY) const {
    // Native panel coordinates are 800x480. These transformations mirror
    // Adafruit_GFX's rotation mapping into Book32's 480x800 portrait space.
    if (nativeX >= 800 || nativeY >= 480) return false;
    if (_rotation == 1) {
        screenX = nativeY;
        screenY = 799 - nativeX;
    } else {
        screenX = 479 - nativeY;
        screenY = nativeX;
    }
    return screenX < SCREEN_WIDTH && screenY < SCREEN_HEIGHT;
}

void DisplayMgr::loadDisplaySettings() {
    int rotation = 3;  // Default: button on the left
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

    int16_t screenW = display.width();
    int16_t screenH = display.height();

    bool drawFullFrame = !_bootScreenActive;
    if (drawFullFrame) {
        display.setFullWindow();
        _bootScreenActive = true;
    } else {
        display.setPartialWindow(60, 410, screenW - 120, 180);
    }

    const int16_t frameInset = 18;
    const int16_t logoY = 185;
    const int16_t titleY = 330;

    display.firstPage();
    do {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);

        if (drawFullFrame) {
            display.fillScreen(GxEPD_WHITE);
            display.drawRect(frameInset, frameInset, screenW - (frameInset * 2), screenH - (frameInset * 2), GxEPD_BLACK);
            display.drawFastHLine(54, 64, screenW - 108, GxEPD_BLACK);
            display.drawFastHLine(54, screenH - 64, screenW - 108, GxEPD_BLACK);

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

            drawCenteredText(display, DEVICE_NAME, &FreeSansBold24pt7b, titleY, GxEPD_BLACK);
            drawCenteredText(display, "Starting Book32 OS", &FreeSans12pt7b, titleY + 42, GxEPD_BLACK);

            char versionText[20];
            snprintf(versionText, sizeof(versionText), "v%s", SYSTEM_VERSION);
            drawCenteredText(display, versionText, &FreeSans9pt7b, screenH - 96, GxEPD_BLACK);
        }

        drawBootProgress(display, progress, status);
    } while (display.nextPage());
}

void DisplayMgr::update() {
    // E-ink usually updates on demand, not every loop
}
