#pragma once
#include "Config.h"
#include <string.h>

// Correct only the old LILYGO default, once. Never replace a custom message
// or re-interpret an explicit choice written by the new settings schema.
inline bool sleepMessageNeedsMigration(int version, const char* message) {
#if defined(BOARD_LILYGO_T5S3_PRO)
    return version < 3 && message && strcmp(message,"Press power to wake") == 0;
#else
    (void)version; (void)message;
    return false;
#endif
}
