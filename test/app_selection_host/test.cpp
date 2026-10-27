// Host test for the installer's app-choice record (AppSelection.h): the
// firmware's CRC-32 must match the web installer's (docs/installer.js).
// Build: c++ -std=c++17 -I test/app_selection_host/stubs -I lib/Book32_Core test/app_selection_host/test.cpp
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "AppSelection.h"

int main() {
    assert(installCrc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);  // the standard check
    // The record installer.js writes for "Todo + Inkade", install ID 0x12345678.
    InstallRecord record;
    memcpy(record.magic, "INKAPPS1", 8);
    record.version = 1;
    record.apps = AppSelection::TODO | AppSelection::INKADE;
    record.reserved = 0;
    record.installId = 0x12345678;
    record.crc = installCrc32(reinterpret_cast<const uint8_t*>(&record), offsetof(InstallRecord, crc));
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    assert(bytes[8] == 1 && bytes[9] == 0x05 && bytes[12] == 0x78 && bytes[15] == 0x12);
    printf("record crc %08x\n", unsigned(record.crc));
    puts("App selection PASS: CRC-32 and record layout");
    return 0;
}
