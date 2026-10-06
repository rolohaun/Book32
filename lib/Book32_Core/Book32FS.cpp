#include "Book32FS.h"
#include "Config.h"

static fs::LittleFSFS internalEbookFS;
fs::FS* EbookFSPtr = &internalEbookFS;
static bool usingSD = false;

#if BOOK32_HAS_SD
#include <SD.h>
#include <SPI.h>
#endif

bool beginEbookStorage(bool internalPartitionBlank) {
#if BOOK32_HAS_SD
#if defined(BOARD_LILYGO_T5S3_PRO)
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(46, OUTPUT); // Deselect LoRa on the shared SPI bus.
    digitalWrite(46, HIGH);
#else
    pinMode(SD_POWER_ENABLE, OUTPUT);
    digitalWrite(SD_POWER_ENABLE, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
#endif
    delay(20);

    if (SD.begin(SD_CS, SPI, 4000000, "/ebooks", 10, false)) {
        EbookFSPtr = &SD;
        usingSD = true;
        Serial.printf("Ebook storage: MicroSD (%llu MB)\n", SD.cardSize() / (1024ULL * 1024ULL));
        return true;
    }
    Serial.println("Ebook storage: no MicroSD mounted; using internal flash fallback");
#endif

    EbookFSPtr = &internalEbookFS;
    usingSD = false;
    bool mounted = internalEbookFS.begin(false, "/ebooks", 10, "ebooks");
    if (!mounted && internalPartitionBlank) {
        Serial.println("EbookFS appears blank; formatting first-use ebook storage...");
        mounted = internalEbookFS.begin(true, "/ebooks", 10, "ebooks");
    }
    return mounted;
}

uint64_t ebookStorageTotalBytes() {
#if BOOK32_HAS_SD
    if (usingSD) return SD.totalBytes();
#endif
    return internalEbookFS.totalBytes();
}

uint64_t ebookStorageUsedBytes() {
#if BOOK32_HAS_SD
    if (usingSD) return SD.usedBytes();
#endif
    return internalEbookFS.usedBytes();
}

bool ebookStorageUsesSD() {
    return usingSD;
}

void endEbookStorageForSleep() {
#if BOOK32_HAS_SD
    if (usingSD) SD.end();
#endif
}
