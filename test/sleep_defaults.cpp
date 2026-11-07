#include <assert.h>
#include <stdio.h>
#include "../lib/Book32_Core/SleepDefaults.h"
int main() {
#if defined(BOARD_LILYGO_T5S3_PRO) && LILYGO_T5S3_PLAIN
    static_assert(PIN_BUTTON == 21, "plain T5-4.7 S3 wakes from the top key, BOOT is the shift register strobe");
    static_assert(SLEEP_CONFIG_VERSION == 3, "message migration schema");
    assert(strcmp(SLEEP_MESSAGE_DEFAULT,"Press the top key to wake")==0);
#elif defined(BOARD_LILYGO_T5S3_PRO)
    static_assert(PIN_BUTTON == 0, "LILYGO BOOT wake is GPIO0");
    static_assert(SLEEP_CONFIG_VERSION == 3, "message migration schema");
    assert(strcmp(SLEEP_MESSAGE_DEFAULT,"Press BOOT to wake")==0);
    assert(sleepMessageNeedsMigration(1,"Press power to wake"));
    assert(sleepMessageNeedsMigration(2,"Press power to wake"));
    assert(!sleepMessageNeedsMigration(3,"Press power to wake"));
    assert(!sleepMessageNeedsMigration(2,"Press BOOT to wake"));
#else
#if defined(BOARD_SEEED_STICKY)
    static_assert(SLEEP_CONFIG_VERSION == 2, "Sticky schema unchanged");
#else
    static_assert(SLEEP_CONFIG_VERSION == 3, "Book32 sleep on by default from v3");
#endif
    assert(strcmp(SLEEP_MESSAGE_DEFAULT,"Press power to wake")==0);
    assert(!sleepMessageNeedsMigration(2,"Press power to wake"));
    assert(!sleepMessageNeedsMigration(3,"Press power to wake"));
    // No BOOT wake on these boards: the LILYGO default is always replaced.
    assert(sleepMessageNeedsMigration(2,"Press BOOT to wake"));
    assert(sleepMessageNeedsMigration(3,"Press BOOT to wake"));
#endif
    assert(!sleepMessageNeedsMigration(2,"Good night!"));
    assert(!sleepMessageNeedsMigration(3,"Good night!"));
    assert(!sleepMessageNeedsMigration(2,nullptr));
    puts("PASS: board-specific sleep default, wrong-board message replaced, custom message preserved");
}
