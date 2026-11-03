#include "HomeFooter.h"
#include "HomeServices.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../../include/Config.h"

namespace HomeFooter {

void drawBattery(Book32Display& display, int x, int baseline) {
    const BatteryStatus bat = BatteryMgr::getInstance().getStatus();
    const int filled = constrain((bat.percentage + 24) / 25, 0, 4);
    for (int bar = 0; bar < 4; ++bar) {
        const int bx = x + bar * 8;
        if (bar < filled) display.fillRect(bx, baseline - 17, 5, 17, GxEPD_BLACK);
        else display.drawRect(bx, baseline - 17, 5, 17, GxEPD_BLACK);
    }
    char text[8];
    snprintf(text, sizeof(text), "%d%%", bat.percentage);
    FontMgr::getInstance().drawText(display, text, x + 40, baseline, FONT_SIZE_BODY, GxEPD_BLACK);
}

void draw(Book32Display& display, bool details) {
    FontMgr& fontMgr = FontMgr::getInstance();
    const int screenW = display.width(), bottomY = display.height() - 10, upperY = bottomY - 30;
    drawBattery(display, MARGIN, bottomY);
    if (!details) return;
    char version[24];
    snprintf(version, sizeof(version), "InkDeck v%s", SYSTEM_VERSION);
    fontMgr.drawText(display, version, MARGIN, upperY, FONT_SIZE_BODY, GxEPD_BLACK);
    const String status = HomeServices::getInstance().footerText();
    if (!status.startsWith("WiFi "))  // an address, not "WiFi offline" or "WiFi starting"
        fontMgr.drawTextRight(display, "Dashboard", screenW - MARGIN, upperY, FONT_SIZE_BODY, GxEPD_BLACK);
    fontMgr.drawTextRight(display, status.c_str(), screenW - MARGIN, bottomY, FONT_SIZE_BODY, GxEPD_BLACK);
}

}
