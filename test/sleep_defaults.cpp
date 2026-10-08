#include <assert.h>
#include <stdio.h>
#include "../lib/Book32_Core/SleepDefaults.h"
int main() {
#if defined(BOARD_LILYGO_T5S3_PRO)
    static_assert(PIN_BUTTON == 0, "LILYGO BOOT wake is GPIO0");
    static_assert(SLEEP_CONFIG_VERSION == 3, "message migration schema");
    assert(strcmp(SLEEP_MESSAGE_DEFAULT,"Press BOOT to wake")==0);
    assert(sleepMessageNeedsMigration(1,"Press power to wake"));
    assert(sleepMessageNeedsMigration(2,"Press power to wake"));
#else
    static_assert(SLEEP_CONFIG_VERSION == 2, "other board schema unchanged");
    assert(strcmp(SLEEP_MESSAGE_DEFAULT,"Press power to wake")==0);
    assert(!sleepMessageNeedsMigration(2,"Press power to wake"));
#endif
    assert(!sleepMessageNeedsMigration(3,"Press power to wake"));
    assert(!sleepMessageNeedsMigration(2,"Good night!"));
    assert(!sleepMessageNeedsMigration(2,"Press BOOT to wake"));
    assert(!sleepMessageNeedsMigration(2,nullptr));
    puts("PASS: board-specific sleep default, legacy migration and custom-message preservation");
}
