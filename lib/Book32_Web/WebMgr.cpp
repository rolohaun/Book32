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
#include "../../include/Config.h"

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
        Serial.printf("EbookFS OK: %llu / %llu bytes used (%s)\n",
                      ebookStorageUsedBytes(), ebookStorageTotalBytes(),
                      ebookStorageUsesSD() ? "MicroSD" : "internal flash");
        listFiles(EbookFS, "/", 1);
    } else {
        Serial.println("ERROR: EbookFS mount failed! Ebooks partition is not available.");
    }
    
    Serial.println("============================\n");
}

void WebMgr::init() {
    if (_initialized) return;
    if (!_endpointsConfigured) {
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

void WebMgr::setupEndpoints() {
    // API: Status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

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

        doc["freeSpace"] = ebookStorageTotalBytes() - ebookStorageUsedBytes();
        doc["totalSpace"] = ebookStorageTotalBytes();
        doc["usedSpace"] = ebookStorageUsedBytes();
        doc["ebookStorage"] = ebookStorageUsesSD() ? "sd" : "internal";
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

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
                if (name.endsWith(".epub") || name.endsWith(".ttf")) {
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
            request->send(200, "text/plain", "Upload Complete");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            static File uploadFile;
            static String savedPath;
            static String originalFilename;

            if (index == 0) {
                originalFilename = filename;
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

                savedPath = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n", savedPath.c_str(), filename.c_str());
                uploadFile = EbookFS.open(savedPath, FILE_WRITE);
                if (!uploadFile) return;
            }

            if (uploadFile && len) {
                uploadFile.write(data, len);
            }

            if (final) {
                if (uploadFile) {
                    uploadFile.close();
                    String truncatedName = savedPath.substring(1); 
                    String origName = originalFilename;
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
                doc["fontFamily"] = savedDoc["fontFamily"] | "native";
                doc["showChapter"] = savedDoc["showChapter"] | true;
                doc["showPageNumber"] = savedDoc["showPageNumber"] | true;
                doc["showReadingPercentage"] = savedDoc["showReadingPercentage"] | true;
            } else {
                doc["refreshFrequency"] = READER_FULL_REFRESH_INTERVAL_DEFAULT;
                doc["fontSize"] = READER_FONT_SIZE_DEFAULT;
                doc["fontFamily"] = "native";
                doc["showChapter"] = true;
                doc["showPageNumber"] = true;
                doc["showReadingPercentage"] = true;
            }
            file.close();
        } else {
            doc["refreshFrequency"] = READER_FULL_REFRESH_INTERVAL_DEFAULT;
            doc["fontSize"] = READER_FONT_SIZE_DEFAULT;
            doc["fontFamily"] = "native";
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
            doc["fontFamily"] = "native";
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
                        doc["fontFamily"] = savedDoc["fontFamily"] | "native";
                        doc["showChapter"] = savedDoc["showChapter"] | true;
                        doc["showPageNumber"] = savedDoc["showPageNumber"] | true;
                        doc["showReadingPercentage"] = savedDoc["showReadingPercentage"] | true;
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
                const char* requestedFamily = json["fontFamily"] | "native";
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

        // Try to load existing settings from EbookFS
        if (EbookFS.exists("/sleep_config.json")) {
            File file = EbookFS.open("/sleep_config.json", "r");
            if (file) {
                DynamicJsonDocument savedDoc(512);
                if (!deserializeJson(savedDoc, file)) {
                    doc["sleepTimeout"] = savedDoc.containsKey("sleepTimeout") ? savedDoc["sleepTimeout"].as<int>() : 0;
                    doc["sleepMessage"] = savedDoc["sleepMessage"] | "Press button to wake";
                } else {
                    doc["sleepTimeout"] = 0;  // Default (disabled)
                    doc["sleepMessage"] = "Press button to wake";
                }
                file.close();
            } else {
                doc["sleepTimeout"] = 0;  // Default (disabled)
                doc["sleepMessage"] = "Press button to wake";
            }
        } else {
            doc["sleepTimeout"] = 0;  // Default: disabled
            doc["sleepMessage"] = "Press button to wake";
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Sleep Settings - POST
    AsyncCallbackJsonWebHandler* sleepSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/sleep",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            DynamicJsonDocument doc(512);

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
