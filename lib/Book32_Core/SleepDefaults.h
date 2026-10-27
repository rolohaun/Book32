#pragma once
#include "Config.h"
#include <stdint.h>
#include <string.h>

// Replace a saved sleep message that is another board's default and wrong for
// this one. Sleep settings live on the SD card, so a card moved between
// devices (or a setting saved from another device) can carry over the wrong
// wake instruction. Never replace a custom message.
inline bool sleepMessageNeedsMigration(int version, const char* message) {
    if (!message) return false;
#if defined(BOARD_LILYGO_T5S3_PRO)
    // The old LILYGO default, once; later it may be an explicit choice.
    return version < 3 && strcmp(message,"Press power to wake") == 0;
#else
    // The Sticky and Book32 wake from their power/function button, never BOOT.
    (void)version;
    return strcmp(message,"Press BOOT to wake") == 0;
#endif
}

// What the sleep screen shows, as saved in sleep_config.json: "cover" (the
// book's cover with the message under it, the default), "coverOnly" or
// "message". Low battery always shows its warning instead.
enum class SleepScreen : uint8_t { COVER_AND_MESSAGE, COVER_ONLY, MESSAGE_ONLY };

inline SleepScreen sleepScreenFromName(const char* name) {
    if (name && strcmp(name, "message") == 0) return SleepScreen::MESSAGE_ONLY;
    if (name && strcmp(name, "coverOnly") == 0) return SleepScreen::COVER_ONLY;
    return SleepScreen::COVER_AND_MESSAGE;
}

inline const char* sleepScreenName(SleepScreen screen) {
    return screen == SleepScreen::MESSAGE_ONLY ? "message" : screen == SleepScreen::COVER_ONLY ? "coverOnly" : "cover";
}

inline const char* sleepScreenLabel(SleepScreen screen) {
    return screen == SleepScreen::MESSAGE_ONLY ? "Sleep message only" :
           screen == SleepScreen::COVER_ONLY ? "Book cover only" : "Book cover and message";
}

// Cover and message, then cover only, then message only, then around again.
inline SleepScreen nextSleepScreen(SleepScreen screen) {
    return screen == SleepScreen::COVER_AND_MESSAGE ? SleepScreen::COVER_ONLY :
           screen == SleepScreen::COVER_ONLY ? SleepScreen::MESSAGE_ONLY : SleepScreen::COVER_AND_MESSAGE;
}
