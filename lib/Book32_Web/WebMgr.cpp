#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include "../Book32_Core/Book32FS.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Apps/AppTodo/AppTodo.h"
#include "../Book32_Core/AppMgr.h"
#include "../Book32_Core/DisplayMgr.h"
#include "../Book32_Core/SoundMgr.h"
#include "../Book32_Core/SleepDefaults.h"
#include "../../include/Config.h"
#include "RomUpload.h"
#include "LilygoControls.h"
#include "../Book32_Core/GamepadMgr.h"
#include "../Book32_Core/AppSelection.h"

static const char* READER_PROGRESS_PATH = "/reader_progress.json";

WebMgr::WebMgr() {
    server = new AsyncWebServer(80);
}

WebMgr& WebMgr::getInstance() {
    static WebMgr instance;
    return instance;
}

static void listFiles(fs::FS &fs, const char * dirname, uint8_t levels) {
#if BOOK32_VERBOSE_BOOT_LOG
    Serial.printf("Listing directory: %s\n", dirname);
    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("- not a directory");
        root.close();
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.printf("  DIR : %s\n", file.name());
            if(levels){
                listFiles(fs, file.path(), levels -1);
            }
        } else {
            Serial.printf("  FILE: %s  SIZE: %d\n", file.name(), file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
#else
    (void)fs;
    (void)dirname;
    (void)levels;
#endif
}

static bool partitionLooksBlank(const esp_partition_t* partition) {
    if (!partition) return false;

    uint8_t buffer[256];
    size_t bytesToCheck = min((size_t)4096, partition->size);
    for (size_t offset = 0; offset < bytesToCheck; offset += sizeof(buffer)) {
        size_t readLen = min(sizeof(buffer), bytesToCheck - offset);
        if (esp_partition_read(partition, offset, buffer, readLen) != ESP_OK) {
            return false;
        }
        for (size_t i = 0; i < readLen; i++) {
            if (buffer[i] != 0xFF) {
                return false;
            }
        }
    }

    return true;
}

void WebMgr::mountFilesystems() {
#if BOOK32_VERBOSE_BOOT_LOG
    Serial.println("\n========== PARTITION TABLE DUMP ==========");
    
    // Iterate through ALL partitions on the chip
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t* part = esp_partition_get(it);
        Serial.printf("  [%s] type=%d subtype=0x%02X addr=0x%06X size=0x%06X (%dKB)\n",
            part->label,
            part->type,
            part->subtype,
            part->address,
            part->size,
            part->size / 1024);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    
    Serial.println("===========================================\n");
#endif
    Serial.println("=== Mounting Filesystems ===");
    
    // Look for "spiffs" partition specifically
    const esp_partition_t* spiffsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (spiffsPart) {
#if BOOK32_VERBOSE_BOOT_LOG
        Serial.printf("Found 'spiffs' partition at 0x%06X, size %dKB\n", spiffsPart->address, spiffsPart->size/1024);
#endif
    } else {
        Serial.println("ERROR: No partition with label 'spiffs' and subtype SPIFFS found!");
        
#if BOOK32_VERBOSE_BOOT_LOG
        // Try to find ANY spiffs-subtype partition
        const esp_partition_t* anySpiffs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (anySpiffs) {
            Serial.printf("Found unlabeled SPIFFS partition '%s' at 0x%06X\n", anySpiffs->label, anySpiffs->address);
        }
#endif
    }
    
    // Mount SystemFS
    bool sysOK = SystemFS.begin(true, "/littlefs", 10, "spiffs");
    if (sysOK) {
        Serial.printf("SystemFS OK: %u / %u bytes used\n", SystemFS.usedBytes(), SystemFS.totalBytes());
        listFiles(SystemFS, "/", 1);
    } else {
        Serial.println("WARNING: SystemFS mount FAILED!");
    }

    // Look for "ebooks" partition (now uses custom subtype 0x82 to avoid uploadfs conflict)
    const esp_partition_t* ebooksPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ebooks");
    if (ebooksPart) {
#if BOOK32_VERBOSE_BOOT_LOG
        Serial.printf("Found 'ebooks' partition at 0x%06X, size %dKB\n", ebooksPart->address, ebooksPart->size/1024);
#endif
    } else {
        Serial.println("WARNING: No partition with label 'ebooks' found!");
    }

    // Sticky prefers its MicroSD card; both models fall back to a dedicated
    // internal LittleFS partition without formatting existing user data.
    bool ebookOK = beginEbookStorage(partitionLooksBlank(ebooksPart));

    if (ebookOK) {
#if BOOK32_VERBOSE_BOOT_LOG
        // usedBytes() walks the whole filesystem (every book on internal
        // flash), so only pay for it when diagnosing storage.
        Serial.printf("EbookFS OK: %llu / %llu bytes used (%s)\n",
                      ebookStorageUsedBytes(), ebookStorageTotalBytes(),
                      ebookStorageUsesSD() ? "MicroSD" : "internal flash");
#else
        Serial.printf("EbookFS OK (%s)\n", ebookStorageUsesSD() ? "MicroSD" : "internal flash");
#endif
        listFiles(EbookFS, "/", 1);
    } else {
        Serial.println("ERROR: EbookFS mount failed! Ebooks partition is not available.");
    }
    
    Serial.println("============================\n");
}

void WebMgr::init() {
    if (_initialized) return;
    if (!_endpointsConfigured) {
#if BOOK32_HAS_INKBOY
        // Cartridge RTC catch-up uses UTC; synchronization is asynchronous.
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
#endif
        setupEndpoints();
        _endpointsConfigured = true;
    }
    server->begin();
    _initialized = true;
    Serial.println("Web Server Started");
}

void WebMgr::stop() {
    if (!_initialized) return;
    server->end();
    _initialized = false;
    Serial.println("Web Server Stopped");
}

void WebMgr::update() {
    // Apply a pending display rotation from the main loop (never draw on the
    // async server task). Repaints whatever app is currently on screen.
    if (_pendingRotation != 0) {
        int rot = _pendingRotation;
        _pendingRotation = 0;
        DisplayMgr::getInstance().setRotation(rot);
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current) current->forceRedraw();
    }

    // Apply a pending reading font-size change from the main loop.
    if (_pendingReaderFontSize != 0) {
        int pt = _pendingReaderFontSize;
        _pendingReaderFontSize = 0;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontSize(pt);
                break;
            }
        }
    }

    if (_pendingReaderFontFamily >= 0) {
        bool useOpenSans = _pendingReaderFontFamily == 1;
        _pendingReaderFontFamily = -1;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontFamily(useOpenSans);
                break;
            }
        }
    }

    // Check if OTA was requested from web UI
    if (_otaPending) {
        _otaPending = false;
        Serial.println("Scheduling OTA update in separate task...");
        
        // Stop the web server to free up async_tcp and memory
        stop();
        delay(100);  // Give time for connections to close
        
        // Create OTA task with 16KB stack (OTA needs significant stack space)
        xTaskCreatePinnedToCore(
            [](void* param) {
                Serial.println("OTA task started");
                GitHubMgr::getInstance().performFullUpdate(SYSTEM_VERSION);
                Serial.println("OTA task complete, restarting...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                ESP.restart();
            },
            "OTA_Task",
            16384,  // 16KB stack
            nullptr,
            1,      // Priority
            nullptr,
            1       // Core 1
        );
    }
}

// Helper: Save original filename to metadata
void saveBookMetadata(const String& truncatedName, const String& originalName) {
    DynamicJsonDocument doc(4096);
    
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (metaFile) {
        DeserializationError error = deserializeJson(doc, metaFile);
        metaFile.close();
        if (error) {
            Serial.println("Failed to parse metadata, creating new");
            doc.clear();
        }
    }
    
    doc[truncatedName] = originalName;
    
    metaFile = SystemFS.open("/books_meta.json", FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    } else {
        Serial.println("Failed to save metadata");
    }
}

String getOriginalFilename(const String& truncatedName) {
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (!metaFile) {
        return truncatedName;
    }
    
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    
    if (error) {
        return truncatedName;
    }
    
    if (doc.containsKey(truncatedName)) {
        return doc[truncatedName].as<String>();
    }
    return truncatedName;
}

void removeBookMetadata(const String& truncatedName) {
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (!metaFile) return;
    
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    
    if (error) return;
    
    doc.remove(truncatedName);
    
    metaFile = SystemFS.open("/books_meta.json", FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
    }
}

void removeBookProgress(const String& filename) {
    if (!EbookFS.exists(READER_PROGRESS_PATH)) return;

    File file = EbookFS.open(READER_PROGRESS_PATH, "r");
    if (!file) return;

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return;

    String path = "/" + filename;
    JsonObject books = doc["books"].as<JsonObject>();
    if (!books.isNull()) {
        books.remove(path);
    }

    String lastBook = doc["lastBook"] | "";
    if (lastBook == path) {
        doc["lastBook"] = "";
        doc["resumeOnBoot"] = false;
    }

    File out = EbookFS.open(READER_PROGRESS_PATH, FILE_WRITE);
    if (out) {
        serializeJson(doc, out);
        out.close();
    }
}

// Book uploads, one at a time. On the touch boards an upload also holds the
// storage lease (RomStorage::Upload), which pauses screen updates: the
// Sticky's card shares its SPI bus with the display, and a redraw in the
// middle of a large upload made the card reject a write.
static AsyncWebServerRequest* bookUploadOwner = nullptr;
static File bookUploadFile;
static String bookUploadPath;
static String bookUploadOriginal;
static bool bookUploadFailed = false;  // A write failed: answer with an error
static bool bookUploadDone = false;
static bool bookUploadLeased = false;
// Network packets are about 1.4 KB; cards write far better in large blocks.
static constexpr size_t BOOK_UPLOAD_BLOCK = 64 * 1024;
static uint8_t* bookUploadBlock = nullptr;
static size_t bookUploadBlockUsed = 0;

// Ends the current upload: an unfinished one (the connection dropped) is
// removed, so no cut-off book is left in the library.
static void endBookUpload() {
    if (bookUploadFile) {
        bookUploadFile.close();
        if (!bookUploadDone) {
            EbookFS.remove(bookUploadPath);
            Serial.printf("Upload interrupted: removed %s\n", bookUploadPath.c_str());
        }
    }
#if BOOK32_HAS_INKBOY
    if (bookUploadLeased) RomStorage::release(RomStorage::Upload);
#endif
    bookUploadLeased = false;
    bookUploadOwner = nullptr;
}

static void bookUploadWriteFailed(size_t index) {
    Serial.printf("Upload failed writing %s at %u bytes\n", bookUploadPath.c_str(), static_cast<unsigned>(index));
    bookUploadFile.close();
    EbookFS.remove(bookUploadPath);
    bookUploadFailed = true;
}

static size_t bookUploadWritten = 0;

// The Sticky's card now and then rejects a write during a long upload (a CRC
// or token error); writing the block again after reopening the file works.
static bool bookUploadWrite(const uint8_t* data, size_t length) {
    for (int attempt = 0; attempt < 6; attempt++) {
        if (attempt) {
            bookUploadFile.close();
            delay(100 * attempt);
            bookUploadFile = EbookFS.open(bookUploadPath, "r+");
            if (!bookUploadFile || !bookUploadFile.seek(bookUploadWritten)) continue;
            Serial.printf("Upload: writing again at %u bytes (attempt %d)\n",
                          static_cast<unsigned>(bookUploadWritten), attempt + 1);
        }
        if (bookUploadFile.write(data, length) == length) {
            bookUploadWritten += length;
            return true;
        }
    }
    return false;
}

static void bookUploadFlush(size_t index) {
    if (bookUploadFile && bookUploadBlockUsed && !bookUploadWrite(bookUploadBlock, bookUploadBlockUsed)) {
        bookUploadWriteFailed(index);
    }
    bookUploadBlockUsed = 0;
}

void WebMgr::setupEndpoints() {
    // API: Status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(768);

        unsigned long totalSeconds = millis() / 1000;
        unsigned long hours = totalSeconds / 3600;
        unsigned long minutes = (totalSeconds % 3600) / 60;
        unsigned long seconds = totalSeconds % 60;
        char uptimeStr[20];
        snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
        doc["uptime"] = uptimeStr;
        doc["uptimeSeconds"] = totalSeconds;

        doc["rssi"] = WiFi.RSSI();
        doc["battery"] = BatteryMgr::getInstance().getPercentage();
        doc["voltage"] = BatteryMgr::getInstance().getVoltage();
        doc["charging"] = BatteryMgr::getInstance().isCharging();
        doc["version"] = SYSTEM_VERSION;
        {
            AppSelection& apps = AppSelection::getInstance();
            JsonObject enabled = doc.createNestedObject("apps");
            enabled["todo"] = apps.enabled(AppSelection::TODO);
            enabled["klipper"] = apps.enabled(AppSelection::KLIPPER);
            enabled["inkade"] = BOOK32_HAS_INKBOY && apps.enabled(AppSelection::INKADE);
        }
#if BOOK32_HAS_INKBOY
        doc["romUpload"] = AppSelection::getInstance().enabled(AppSelection::INKADE);
        doc["romMaxBytes"] = INK_ROM_MAX_BYTES;
#endif
#if defined(BOARD_LILYGO_T5S3_PRO)
        doc["lilygoControls"] = true;
#endif
#if BOOK32_HAS_PAGE_BOOKS
        // PDFs and CBZs are converted in the browser to pages of this size.
        doc["pageBooks"] = true;
        doc["pageWidth"] = 2 * SCREEN_WIDTH;
        doc["pageHeight"] = 2 * SCREEN_HEIGHT;
#endif

        doc["freeSpace"] = ebookStorageTotalBytes() - ebookStorageUsedBytes();
        doc["totalSpace"] = ebookStorageTotalBytes();
        doc["usedSpace"] = ebookStorageUsedBytes();
        doc["ebookStorage"] = ebookStorageUsesSD() ? "sd" : "internal";
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

#if BOOK32_HAS_INKBOY
    RomUpload::configure(server);
#endif
    // API: List Books from EbookFS
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(2048);
        JsonArray books = doc.createNestedArray("books");

        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                if (name.endsWith(".epub") || name.endsWith(".ttf") || name.endsWith(".cbz")) {
                    JsonObject book = books.createNestedObject();
                    String displayName = getOriginalFilename(name);
                    book["name"] = displayName;
                    book["filename"] = name;
                    book["size"] = file.size();
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Upload Book to EbookFS
    server->on("/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (request->_tempObject) {  // Refused at the start (see below)
                request->send(409, "text/plain", "Another upload or a game is using the storage card. Try again in a moment.");
                return;
            }
            const bool saved = bookUploadOwner == request && bookUploadDone && !bookUploadFailed;
            if (bookUploadOwner == request) endBookUpload();
            if (saved) request->send(200, "text/plain", "Upload Complete");
            else request->send(500, "text/plain", "The book could not be saved. Check the storage card and free space.");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                bool refused = bookUploadOwner != nullptr;
#if BOOK32_HAS_INKBOY
                if (!refused) {
                    refused = !RomStorage::acquire(RomStorage::Upload);
                    bookUploadLeased = !refused;
                }
#endif
                if (refused) {
                    if (!request->_tempObject) request->_tempObject = calloc(1, 1);  // Freed by the server
                    return;
                }
                bookUploadOwner = request;
                request->onDisconnect([request]() {
                    if (bookUploadOwner == request) endBookUpload();
                });
                if (request->client()) {
                    // A card can pause for a few seconds while it reorganizes;
                    // don't drop the connection over it (the default is 3 s).
                    request->client()->setRxTimeout(30);
                }
                if (!bookUploadBlock) bookUploadBlock = static_cast<uint8_t*>(ps_malloc(BOOK_UPLOAD_BLOCK));
                bookUploadBlockUsed = 0;
                bookUploadWritten = 0;
                bookUploadFailed = false;
                bookUploadDone = false;
                bookUploadOriginal = filename;
                String safeName = filename;

                int lastSlash = safeName.lastIndexOf('/');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);
                lastSlash = safeName.lastIndexOf('\\');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);

                if (safeName.length() > 28) {
                    int dotPos = safeName.lastIndexOf('.');
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    safeName = safeName.substring(0, 28 - ext.length()) + ext;
                }

                String testPath = "/" + safeName;
                if (EbookFS.exists(testPath)) {
                    int dotPos = safeName.lastIndexOf('.');
                    String baseName = (dotPos != -1) ? safeName.substring(0, dotPos) : safeName;
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    
                    if (baseName.length() > 20) baseName = baseName.substring(0, 20);

                    int suffix = 1;
                    while (suffix < 100) {
                        safeName = baseName + "_" + String(suffix) + ext;
                        testPath = "/" + safeName;
                        if (!EbookFS.exists(testPath)) break;
                        suffix++;
                    }
                }

                bookUploadPath = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n", bookUploadPath.c_str(), filename.c_str());
                bookUploadFile = EbookFS.open(bookUploadPath, FILE_WRITE);
                if (!bookUploadFile) bookUploadFailed = true;
            }
            if (bookUploadOwner != request) return;

            // A large manga upload takes minutes: it counts as activity, so
            // idle sleep never cuts it off.
            BatteryMgr::getInstance().resetIdleTimer();
            if (bookUploadFile && len) {
                if (!bookUploadBlock) {
                    if (!bookUploadWrite(data, len)) bookUploadWriteFailed(index);
                } else {
                    for (size_t done = 0; bookUploadFile && done < len;) {
                        const size_t take = min(len - done, BOOK_UPLOAD_BLOCK - bookUploadBlockUsed);
                        memcpy(bookUploadBlock + bookUploadBlockUsed, data + done, take);
                        bookUploadBlockUsed += take;
                        done += take;
                        if (bookUploadBlockUsed == BOOK_UPLOAD_BLOCK) bookUploadFlush(index);
                    }
                }
            }

            if (final) {
                if (bookUploadBlock) bookUploadFlush(index);
                if (bookUploadFile) {
                    bookUploadFile.close();
                    bookUploadDone = true;
                    String truncatedName = bookUploadPath.substring(1);
                    String origName = bookUploadOriginal;
                    int lastSlash = origName.lastIndexOf('/');
                    if (lastSlash >= 0) origName = origName.substring(lastSlash + 1);
                    lastSlash = origName.lastIndexOf('\\');
                    if (lastSlash >= 0) origName = origName.substring(lastSlash + 1);

                    saveBookMetadata(truncatedName, origName);

                }
            }
        }
    );

    // API: Delete Book from EbookFS
    server->on("/api/books/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "Missing name param");
            return;
        }

        String filename = request->getParam("name")->value();
        String path = "/" + filename;

        if (EbookFS.exists(path)) {
            if (EbookFS.remove(path)) {
                Serial.printf("Deleted: %s\n", path.c_str());
                removeBookMetadata(filename);
                removeBookProgress(filename);
                String thumbPath = "/covers/" + filename;
                thumbPath.replace(".epub", ".thumb");
                if (EbookFS.exists(thumbPath)) EbookFS.remove(thumbPath);
                String coverPath = "/covers/" + filename;
                coverPath.replace(".epub", ".cover");
                coverPath.replace(".EPUB", ".cover");
                if (EbookFS.exists(coverPath)) EbookFS.remove(coverPath);
                String cover2Path = "/covers/" + filename;
                cover2Path.replace(".epub", ".cover2");
                cover2Path.replace(".EPUB", ".cover2");
                if (EbookFS.exists(cover2Path)) EbookFS.remove(cover2Path);
                request->send(200, "text/plain", "Deleted");
            } else {
                request->send(500, "text/plain", "Delete failed");
            }
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

    // API: Check for Updates
    server->on("/api/check_update", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        Serial.println("Checking for updates...");
        UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);

        doc["hasUpdate"] = info.available;
        doc["latest"] = info.version;
        doc["current"] = SYSTEM_VERSION;
        doc["hasFirmware"] = info.hasFirmware;
        doc["hasFilesystem"] = info.hasFilesystem;
        doc["release_notes"] = info.notes;

        if (info.available) {
            Serial.printf("Update available: %s\n", info.version.c_str());
        } else {
            Serial.println("No update available");
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Perform Full Update (firmware + filesystem)
    // Sets flag to perform OTA from main loop (avoids blocking async_tcp)
    server->on("/api/update/all", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Update scheduled - will start in a moment");
        Serial.println("OTA update requested via web UI, scheduling...");
        WebMgr::getInstance()._otaPending = true;
    });

    // API: Reader Settings - GET
    server->on("/api/settings/reader", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        // Try to load existing settings
        File file;
        if (EbookFS.exists("/reader_config.json")) {
            file = EbookFS.open("/reader_config.json", "r");
        } else if (SystemFS.exists("/reader_config.json")) {
            file = SystemFS.open("/reader_config.json", "r");
        }

        if (file) {
            DynamicJsonDocument savedDoc(512);
            if (!deserializeJson(savedDoc, file)) {
                doc["refreshFrequency"] = savedDoc["refreshFrequency"] | READER_FULL_REFRESH_INTERVAL_DEFAULT;
                doc["fontSize"] = savedDoc["fontSize"] | READER_FONT_SIZE_DEFAULT;
                doc["fontFamily"] = savedDoc["fontFamily"] | READER_FONT_FAMILY_DEFAULT;
                doc["showChapter"] = savedDoc["showChapter"] | true;
                doc["showPageNumber"] = savedDoc["showPageNumber"] | true;
                doc["showReadingPercentage"] = savedDoc["showReadingPercentage"] | true;
                doc["mangaRightToLeft"] = savedDoc["mangaRightToLeft"] | false;
                doc["grayPages"] = savedDoc["grayPages"] | true;
                doc["grayZoom"] = savedDoc["grayZoom"] | false;
            } else {
                doc["refreshFrequency"] = READER_FULL_REFRESH_INTERVAL_DEFAULT;
                doc["fontSize"] = READER_FONT_SIZE_DEFAULT;
                doc["fontFamily"] = READER_FONT_FAMILY_DEFAULT;
                doc["showChapter"] = true;
                doc["showPageNumber"] = true;
                doc["showReadingPercentage"] = true;
            }
            file.close();
        } else {
            doc["refreshFrequency"] = READER_FULL_REFRESH_INTERVAL_DEFAULT;
            doc["fontSize"] = READER_FONT_SIZE_DEFAULT;
            doc["fontFamily"] = READER_FONT_FAMILY_DEFAULT;
            doc["showChapter"] = true;
            doc["showPageNumber"] = true;
            doc["showReadingPercentage"] = true;
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Reader Settings - POST
    AsyncCallbackJsonWebHandler* readerSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/reader",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            // Merge into the existing config so one setting doesn't wipe the other.
            DynamicJsonDocument doc(512);
            doc["refreshFrequency"] = READER_FULL_REFRESH_INTERVAL_DEFAULT;
            doc["fontSize"] = READER_FONT_SIZE_DEFAULT;
            doc["fontFamily"] = READER_FONT_FAMILY_DEFAULT;
            doc["showChapter"] = true;
            doc["showPageNumber"] = true;
            doc["showReadingPercentage"] = true;
            if (EbookFS.exists("/reader_config.json")) {
                File existing = EbookFS.open("/reader_config.json", "r");
                if (existing) {
                    DynamicJsonDocument savedDoc(512);
                    if (!deserializeJson(savedDoc, existing)) {
                        doc["refreshFrequency"] = savedDoc["refreshFrequency"] | READER_FULL_REFRESH_INTERVAL_DEFAULT;
                        doc["fontSize"] = savedDoc["fontSize"] | READER_FONT_SIZE_DEFAULT;
                        doc["fontFamily"] = savedDoc["fontFamily"] | READER_FONT_FAMILY_DEFAULT;
                        doc["showChapter"] = savedDoc["showChapter"] | true;
                        doc["showPageNumber"] = savedDoc["showPageNumber"] | true;
                        doc["showReadingPercentage"] = savedDoc["showReadingPercentage"] | true;
                        doc["mangaRightToLeft"] = savedDoc["mangaRightToLeft"] | false;
                        doc["grayPages"] = savedDoc["grayPages"] | true;
                        doc["grayZoom"] = savedDoc["grayZoom"] | false;
                    }
                    existing.close();
                }
            }

            if (json.containsKey("refreshFrequency")) {
                doc["refreshFrequency"] = json["refreshFrequency"].as<int>();
            }
            if (json.containsKey("fontSize")) {
                int pt = json["fontSize"].as<int>();
                pt = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);  // Clamp to supported sizes
                doc["fontSize"] = pt;
                // Apply live from the main loop if a book is open.
                WebMgr::getInstance()._pendingReaderFontSize = pt;
            }
            if (json.containsKey("fontFamily")) {
                const char* requestedFamily = json["fontFamily"] | READER_FONT_FAMILY_DEFAULT;
                bool useOpenSans = strcmp(requestedFamily, "openSans") == 0;
                doc["fontFamily"] = useOpenSans ? "openSans" : "native";
                WebMgr::getInstance()._pendingReaderFontFamily = useOpenSans ? 1 : 0;
            }
            if (json.containsKey("showChapter")) {
                doc["showChapter"] = json["showChapter"].as<bool>();
            }
            if (json.containsKey("showPageNumber")) {
                doc["showPageNumber"] = json["showPageNumber"].as<bool>();
            }
            if (json.containsKey("showReadingPercentage")) {
                doc["showReadingPercentage"] = json["showReadingPercentage"].as<bool>();
            }
            if (json.containsKey("mangaRightToLeft")) {
                doc["mangaRightToLeft"] = json["mangaRightToLeft"].as<bool>();
            }
            if (json.containsKey("grayPages")) {
                doc["grayPages"] = json["grayPages"].as<bool>();
            }
            if (json.containsKey("grayZoom")) {
                doc["grayZoom"] = json["grayZoom"].as<bool>();
            }

            // Save to EbookFS (primary storage - persists through OTA)
            File file = EbookFS.open("/reader_config.json", FILE_WRITE);
            if (file) {
                serializeJson(doc, file);
                file.close();
                Serial.printf("Saved reader settings: refreshFrequency=%d, fontSize=%d, fontFamily=%s\n",
                             doc["refreshFrequency"].as<int>(), doc["fontSize"].as<int>(),
                             doc["fontFamily"].as<const char*>());
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(readerSettingsHandler);

    // API: Display Settings (orientation) - GET
    server->on("/api/settings/display", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(128);

        int rotation = 3;  // Default: buttons on the right
        if (EbookFS.exists("/display_config.json")) {
            File file = EbookFS.open("/display_config.json", "r");
            if (file) {
                DynamicJsonDocument savedDoc(128);
                if (!deserializeJson(savedDoc, file)) {
                    rotation = savedDoc["rotation"] | 3;
                }
                file.close();
            }
        }
        doc["rotation"] = rotation;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Display Settings (orientation) - POST. Applies OS-wide; persisted on
    // EbookFS so it survives OTA updates.
    AsyncCallbackJsonWebHandler* displaySettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/display",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            int rotation = json["rotation"] | 3;
            if (rotation != 1 && rotation != 3) rotation = 3;  // Portrait orientations only

            DynamicJsonDocument doc(128);
            doc["rotation"] = rotation;

            File file = EbookFS.open("/display_config.json", FILE_WRITE);
            if (file) {
                serializeJson(doc, file);
                file.close();
                // Apply from the main loop (never rotate/draw on the async task).
                WebMgr::getInstance()._pendingRotation = rotation;
                Serial.printf("Saved display settings: rotation=%d\n", rotation);
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(displaySettingsHandler);

    // API: Optional apps. Saving restarts InkDeck, which registers its apps at
    // boot; with none the library becomes the home screen.
    server->on("/api/apps", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);
        AppSelection& apps = AppSelection::getInstance();
        doc["todo"] = apps.enabled(AppSelection::TODO);
        doc["klipper"] = apps.enabled(AppSelection::KLIPPER);
        doc["inkade"] = BOOK32_HAS_INKBOY && apps.enabled(AppSelection::INKADE);
        doc["inkadeAvailable"] = bool(BOOK32_HAS_INKBOY);
        serializeJson(doc, *response);
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* appsHandler = new AsyncCallbackJsonWebHandler("/api/apps",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            uint8_t mask = 0;
            if (json["todo"] | false) mask |= AppSelection::TODO;
            if (json["klipper"] | false) mask |= AppSelection::KLIPPER;
            if (json["inkade"] | false) mask |= AppSelection::INKADE;
            if (!AppSelection::getInstance().save(mask)) {
                request->send(500, "application/json", "{\"status\":\"error\"}");
                return;
            }
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            Serial.printf("Apps: saved 0x%02X from the web page; restarting\n", mask);
            xTaskCreate([](void*) {
                vTaskDelay(pdMS_TO_TICKS(1500));  // Let the reply reach the browser
                ESP.restart();
            }, "AppsRestart", 2048, nullptr, 1, nullptr);
        }
    );
    server->addHandler(appsHandler);

    // API: Touch sounds. Unsupported hardware reports itself so the web UI can
    // hide the control instead of presenting a setting that cannot take effect.
    server->on("/api/settings/sound", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(128);
        SoundMgr& sound = SoundMgr::getInstance();
        doc["supported"] = sound.isSupported();
        doc["enabled"] = sound.isEnabled();
        serializeJson(doc, *response);
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* soundSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/sound",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            SoundMgr& sound = SoundMgr::getInstance();
            if (!sound.isSupported()) {
                request->send(400, "application/json", "{\"status\":\"unsupported\"}");
                return;
            }
            bool enabled = json["enabled"] | true;
            sound.setEnabled(enabled);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );
    server->addHandler(soundSettingsHandler);

#if defined(BOARD_LILYGO_T5S3_PRO)
    server->on("/api/settings/lilygo", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto& c = LilygoControls::instance();
        auto* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(192);
        doc["brightness"] = c.brightness(); doc["front"] = int(c.frontAction()); doc["side"] = int(c.sideAction());
        serializeJson(doc, *response); request->send(response);
    });
    auto* lilygoSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/lilygo",
        [](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!json["brightness"].is<int>() || !json["front"].is<int>() || !json["side"].is<int>()) {
                request->send(400,"application/json","{\"status\":\"invalid\"}"); return;
            }
            int brightness = json["brightness"], front = json["front"], side = json["side"];
            if (brightness < 10 || brightness > 100 || front < 0 || front >= LilygoControls::ACTION_COUNT ||
                side < 0 || side >= LilygoControls::ACTION_COUNT) {
                request->send(400,"application/json","{\"status\":\"invalid\"}"); return;
            }
            // GPIO/LEDC updates happen in the main loop, never in this web task.
            bool ok = LilygoControls::instance().save(brightness,front,side);
            request->send(ok ? 200 : 500,"application/json",ok ? "{\"status\":\"ok\"}" : "{\"status\":\"save_failed\"}");
        });
    lilygoSettingsHandler->setMethod(HTTP_POST);
    server->addHandler(lilygoSettingsHandler);
#endif

    // API: Reader Progress - GET
    server->on("/api/reader/progress", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        doc["exists"] = false;
        doc["resumeOnBoot"] = false;

        if (EbookFS.exists(READER_PROGRESS_PATH)) {
            File file = EbookFS.open(READER_PROGRESS_PATH, "r");
            if (file) {
                DynamicJsonDocument savedDoc(4096);
                DeserializationError error = deserializeJson(savedDoc, file);
                file.close();

                if (!error) {
                    String lastBook = savedDoc["lastBook"] | "";
                    doc["exists"] = lastBook.length() > 0;
                    doc["lastBook"] = lastBook;
                    doc["displayName"] = lastBook.length() > 1 ? getOriginalFilename(lastBook.substring(1)) : "";
                    doc["resumeOnBoot"] = savedDoc["resumeOnBoot"] | false;

                    JsonObject books = savedDoc["books"].as<JsonObject>();
                    if (!books.isNull() && books.containsKey(lastBook)) {
                        JsonObject progress = books[lastBook];
                        doc["chapter"] = progress["chapter"] | 0;
                        doc["page"] = progress["globalPage"] | 1;
                    }
                }
            }
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Reader Progress - DELETE
    server->on("/api/reader/progress", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (EbookFS.exists(READER_PROGRESS_PATH)) {
            if (!EbookFS.remove(READER_PROGRESS_PATH)) {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to reset progress\"}");
                return;
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Klipper Settings - GET
    server->on("/api/settings/klipper", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);

        // Try to load existing settings from EbookFS
        if (EbookFS.exists("/klipper_config.json")) {
            File file = EbookFS.open("/klipper_config.json", "r");
            if (file) {
                DynamicJsonDocument savedDoc(256);
                if (!deserializeJson(savedDoc, file)) {
                    doc["fullRefreshInterval"] = savedDoc["fullRefreshInterval"] | 5;
                    doc["statusUpdateInterval"] = savedDoc["statusUpdateInterval"] | 30;
                } else {
                    doc["fullRefreshInterval"] = 5;
                    doc["statusUpdateInterval"] = 30;
                }
                file.close();
            } else {
                doc["fullRefreshInterval"] = 5;
                doc["statusUpdateInterval"] = 30;
            }
        } else {
            doc["fullRefreshInterval"] = 5;  // Default: 5 minutes
            doc["statusUpdateInterval"] = 30;  // Default: 30 seconds
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Klipper Settings - POST
    AsyncCallbackJsonWebHandler* klipperSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/klipper",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            DynamicJsonDocument doc(256);

            if (json.containsKey("fullRefreshInterval")) {
                doc["fullRefreshInterval"] = json["fullRefreshInterval"].as<int>();
            }
            if (json.containsKey("statusUpdateInterval")) {
                doc["statusUpdateInterval"] = json["statusUpdateInterval"].as<int>();
            }

            // Save to EbookFS (persists through OTA updates)
            File file = EbookFS.open("/klipper_config.json", FILE_WRITE);
            if (file) {
                serializeJson(doc, file);
                file.close();
                Serial.printf("Saved Klipper settings: fullRefreshInterval=%d min, statusUpdateInterval=%d sec\n",
                             doc["fullRefreshInterval"].as<int>(), doc["statusUpdateInterval"].as<int>());
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(klipperSettingsHandler);

    // API: Sleep Settings - GET
    server->on("/api/settings/sleep", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);
        doc["defaultSleepMessage"] = SLEEP_MESSAGE_DEFAULT;
        doc["sleepScreen"] = "cover";  // default; replaced by the saved choice below

        // Try to load existing settings from EbookFS
        if (EbookFS.exists("/sleep_config.json")) {
            File file = EbookFS.open("/sleep_config.json", "r");
            if (file) {
                DynamicJsonDocument savedDoc(512);
                if (!deserializeJson(savedDoc, file)) {
                    doc["sleepTimeout"] = savedDoc.containsKey("sleepTimeout") ? savedDoc["sleepTimeout"].as<int>() : SLEEP_TIMEOUT_DEFAULT_MINUTES;
                    const char* savedMessage = savedDoc["sleepMessage"] | SLEEP_MESSAGE_DEFAULT;
                    doc["sleepMessage"] = sleepMessageNeedsMigration(savedDoc["configVersion"] | 1, savedMessage)
                                               ? SLEEP_MESSAGE_DEFAULT : savedMessage;
                    doc["sleepScreen"] = savedDoc["sleepScreen"] | "cover";
                } else {
                    doc["sleepTimeout"] = SLEEP_TIMEOUT_DEFAULT_MINUTES;
                    doc["sleepMessage"] = SLEEP_MESSAGE_DEFAULT;
                }
                file.close();
            } else {
                doc["sleepTimeout"] = SLEEP_TIMEOUT_DEFAULT_MINUTES;
                doc["sleepMessage"] = SLEEP_MESSAGE_DEFAULT;
            }
        } else {
            doc["sleepTimeout"] = SLEEP_TIMEOUT_DEFAULT_MINUTES;
            doc["sleepMessage"] = SLEEP_MESSAGE_DEFAULT;
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Sleep Settings - POST
    AsyncCallbackJsonWebHandler* sleepSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/sleep",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            // Merge into the saved settings so a partial save (or an older
            // cached page) never drops a setting it does not know about.
            DynamicJsonDocument doc(512);
            if (EbookFS.exists("/sleep_config.json")) {
                File existing = EbookFS.open("/sleep_config.json", "r");
                if (existing) { deserializeJson(doc, existing); existing.close(); }
            }
            doc["configVersion"] = SLEEP_CONFIG_VERSION;

            if (json.containsKey("sleepScreen")) {
                doc["sleepScreen"] = strcmp(json["sleepScreen"] | "cover", "message") == 0 ? "message" : "cover";
            }
            if (json.containsKey("sleepTimeout")) {
                doc["sleepTimeout"] = json["sleepTimeout"].as<int>();
            }
            if (json.containsKey("sleepMessage")) {
                doc["sleepMessage"] = json["sleepMessage"].as<String>();
            }

            // Save to EbookFS (persists through OTA updates)
            File file = EbookFS.open("/sleep_config.json", FILE_WRITE);
            if (file) {
                serializeJson(doc, file);
                file.close();
                Serial.printf("Saved sleep settings: timeout=%d min, message=%s\n",
                             doc["sleepTimeout"].as<int>(), doc["sleepMessage"].as<String>().c_str());

                // Notify BatteryMgr to reload settings
                BatteryMgr::getInstance().loadSleepSettings();

                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(sleepSettingsHandler);

    // API: Bluetooth gamepad - status, scan results
    server->on("/api/gamepad", HTTP_GET, [](AsyncWebServerRequest *request) {
        GamepadMgr& pad = GamepadMgr::getInstance();
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(4096);
        doc["state"] = pad.stateName();
        doc["name"] = pad.savedName();
        doc["address"] = pad.savedAddress();
        doc["battery"] = pad.battery();
        doc["buttons"] = pad.buttons();
        doc["raw"] = pad.rawButtons();
        JsonArray found = doc.createNestedArray("found");
        for (const GamepadMgr::Found& f : pad.found()) {
            JsonObject o = found.createNestedObject();
            o["name"] = f.name;
            o["address"] = f.address;
            o["type"] = f.addressType;
            o["rssi"] = f.rssi;
            o["gamepad"] = f.gamepad;
        }
        serializeJson(doc, *response);
        request->send(response);
    });
    server->on("/api/gamepad/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        GamepadMgr::getInstance().scan(10);
        request->send(200, "application/json", "{\"status\":\"scanning\"}");
    });
    server->on("/api/gamepad/forget", HTTP_POST, [](AsyncWebServerRequest *request) {
        GamepadMgr::getInstance().forget();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    AsyncCallbackJsonWebHandler* gamepadPairHandler = new AsyncCallbackJsonWebHandler("/api/gamepad/pair",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            const String address = json["address"] | "";
            if (address.length() < 17) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing address\"}");
                return;
            }
            GamepadMgr::getInstance().pair(address, json["type"] | 0, json["name"] | "");
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );
    server->addHandler(gamepadPairHandler);

    // API: Switch to app by name
    server->on("/api/app/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"App name required\"}");
            return;
        }

        String appName = request->getParam("name")->value();
        AppMgr& appMgr = AppMgr::getInstance();

        int appIndex = -1;
        int idx = 0;
        for (auto* app : appMgr.getApps()) {
            if (appName.equalsIgnoreCase(app->getName())) {
                appIndex = idx;
                break;
            }
            idx++;
        }

        if (appIndex >= 0) {
            appMgr.switchTo(appIndex);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            Serial.printf("Switched to app: %s\n", appName.c_str());
        } else {
            request->send(404, "application/json", "{\"error\":\"App not found\"}");
        }
    });

    // === TODO API ENDPOINTS ===

    // Helper to get AppTodo instance
    auto getTodoApp = []() -> AppTodo* {
        AppMgr& appMgr = AppMgr::getInstance();
        for (auto* app : appMgr.getApps()) {
            if (strcmp(app->getName(), "Todo") == 0) {
                return static_cast<AppTodo*>(app);
            }
        }
        return nullptr;
    };

    // GET /api/todos - List all todos
    server->on("/api/todos", HTTP_GET, [getTodoApp](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("todos");

        AppTodo* todoApp = getTodoApp();
        if (todoApp) {
            for (const auto& item : todoApp->getTodos()) {
                JsonObject obj = arr.createNestedObject();
                obj["id"] = item.id;
                obj["text"] = item.text;
                obj["completed"] = item.completed;
            }
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // NOTE: Register more specific routes FIRST to avoid prefix matching issues

    // POST /api/todos/toggle - Toggle a todo's completed status
    AsyncCallbackJsonWebHandler* toggleTodoHandler = new AsyncCallbackJsonWebHandler("/api/todos/toggle",
        [getTodoApp](AsyncWebServerRequest *request, JsonVariant &json) {
            AppTodo* todoApp = getTodoApp();
            if (!todoApp) {
                request->send(500, "application/json", "{\"error\":\"Todo app not found\"}");
                return;
            }

            int id = json["id"] | -1;
            if (id < 0) {
                request->send(400, "application/json", "{\"error\":\"Valid ID is required\"}");
                return;
            }

            todoApp->toggleTodo(id);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );
    server->addHandler(toggleTodoHandler);

    // POST /api/todos/edit - Edit a todo's text
    AsyncCallbackJsonWebHandler* editTodoHandler = new AsyncCallbackJsonWebHandler("/api/todos/edit",
        [getTodoApp](AsyncWebServerRequest *request, JsonVariant &json) {
            AppTodo* todoApp = getTodoApp();
            if (!todoApp) {
                request->send(500, "application/json", "{\"error\":\"Todo app not found\"}");
                return;
            }

            int id = json["id"] | -1;
            String text = json["text"].as<String>();
            if (id < 0 || text.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Valid ID and text are required\"}");
                return;
            }

            todoApp->editTodo(id, text);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );
    server->addHandler(editTodoHandler);

    // POST /api/todos/add - Add a new todo (use specific path to avoid conflicts)
    AsyncCallbackJsonWebHandler* addTodoHandler = new AsyncCallbackJsonWebHandler("/api/todos/add",
        [getTodoApp](AsyncWebServerRequest *request, JsonVariant &json) {
            AppTodo* todoApp = getTodoApp();
            if (!todoApp) {
                request->send(500, "application/json", "{\"error\":\"Todo app not found\"}");
                return;
            }

            String text = json["text"].as<String>();
            if (text.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"Text is required\"}");
                return;
            }

            todoApp->addTodo(text);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );
    server->addHandler(addTodoHandler);

    // DELETE /api/todos - Delete a todo
    server->on("/api/todos/delete", HTTP_DELETE, [getTodoApp](AsyncWebServerRequest *request) {
        AppTodo* todoApp = getTodoApp();
        if (!todoApp) {
            request->send(500, "application/json", "{\"error\":\"Todo app not found\"}");
            return;
        }

        if (!request->hasParam("id")) {
            request->send(400, "application/json", "{\"error\":\"ID parameter is required\"}");
            return;
        }

        int id = request->getParam("id")->value().toInt();
        todoApp->deleteTodo(id);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // === WIFI / HOTSPOT API ENDPOINTS ===

    // API: WiFi status - station connection + hotspot (AP) state
    server->on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        bool sta = WiFi.status() == WL_CONNECTED;
        doc["sta_connected"] = sta;
        doc["sta_ssid"] = sta ? WiFi.SSID() : String("");
        doc["sta_ip"] = sta ? WiFi.localIP().toString() : String("");
        doc["rssi"] = sta ? WiFi.RSSI() : 0;

        wifi_mode_t mode = WiFi.getMode();
        bool ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["ap_active"] = ap;
        doc["ap_ssid"] = ap ? WiFi.softAPSSID() : String("");
        doc["ap_ip"] = ap ? WiFi.softAPIP().toString() : String("");

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: WiFi scan - async so async_tcp keeps running. First call kicks off a
    // scan and returns 202; the client polls until results are ready.
    server->on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);  // start async scan
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }

        // n >= 0: results available
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n && i < 20; i++) {
            JsonObject net = arr.createNestedObject();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        serializeJson(doc, *response);
        request->send(response);
        WiFi.scanDelete();
    });

    // API: WiFi connect - join a network. Credentials persist to NVS so the
    // device reconnects automatically on the next boot.
    AsyncCallbackJsonWebHandler* wifiConnectHandler = new AsyncCallbackJsonWebHandler("/api/wifi/connect",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            String ssid = json["ssid"].as<String>();
            String password = json["password"] | "";

            if (ssid.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"SSID is required\"}");
                return;
            }

            // Keep the AP up (AP_STA) so the phone stays connected to the page
            // while the station connection is attempted.
            wifi_mode_t mode = WiFi.getMode();
            if (mode == WIFI_AP) WiFi.mode(WIFI_AP_STA);
            else if (mode == WIFI_OFF) WiFi.mode(WIFI_STA);

            Serial.printf("WiFi connect requested via web: %s\n", ssid.c_str());
            WiFi.begin(ssid.c_str(), password.c_str());

            request->send(200, "application/json", "{\"status\":\"connecting\"}");
        }
    );
    server->addHandler(wifiConnectHandler);

    // Static Files - serve from SystemFS first (where OTA filesystem updates go)
    // Fall back to EbookFS if not found
    if (SystemFS.exists("/index.html")) {
        Serial.println("Serving web UI from SystemFS");
        server->serveStatic("/", SystemFS, "/").setDefaultFile("index.html");
    } else if (EbookFS.exists("/index.html")) {
        Serial.println("Serving web UI from EbookFS");
        server->serveStatic("/", EbookFS, "/").setDefaultFile("index.html");
    } else {
        Serial.println("WARNING: No index.html found on either filesystem!");
    }
}
