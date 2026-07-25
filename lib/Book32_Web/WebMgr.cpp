#include "WebMgr.h"
#include "../Book32_Core/SettingsStore.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <vector>
#include "../Book32_Core/Book32FS.h"
#include "../Book32_Core/FileExt.h"
#include "../Book32_Core/SafeName.h"
#include "../Book32_Core/UploadGuard.h"
#include "../Book32_Core/DeviceCred.h"
#include "../Book32_Core/BookOrderLogic.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/AppMgr.h"
#include "../Book32_Core/DisplayMgr.h"
#include "../../include/Config.h"

static const char* READER_PROGRESS_PATH = "/reader_progress.json";

// v1.5.0 (security): HTTP Basic Auth.
//
// Scope is decided by *effect*, not by HTTP verb. Every POST/DELETE is
// protected, plus two GETs that are not read-only in practice:
//   * /api/app/switch  — declared HTTP_GET but changes device state.
//   * /api/wifi/status and /api/wifi/scan — leak the home network SSID, the
//     local IP and the list of neighbouring networks.
// Left open so the web UI can load and render before prompting for
// credentials: /api/status, /api/books, the settings GETs and the reader
// progress GET.
//
// Caveat: Basic Auth is base64, not encryption, and this server is plain HTTP.
// This stops casual access; it is not confidential against a LAN sniffer.
//
// v1.6.2: the password is a fixed literal (see DeviceCred.h) instead of being
// derived from the MAC, so it no longer has to be read off the device before
// the web interface can be used.
const char* WebMgr::devicePassword() {
    return BOOK32_DEVICE_PASSWORD;
}

// Returns true when the request is authorised. On failure it has already sent
// a 401 challenge, so the caller must simply return.
static bool requireAuth(AsyncWebServerRequest* request) {
    if (request->authenticate(BOOK32_AUTH_USER, WebMgr::devicePassword())) {
        return true;
    }
    // v1.6.3 (fix): requestAuthentication()'s isDigest parameter defaults to
    // true, so the bare call announced `WWW-Authenticate: Digest`. The browser
    // obeyed the challenge and answered in Digest, which this library fails to
    // validate — correct credentials were rejected and the login prompt kept
    // reappearing. Everything else here (and in script.js) assumes Basic, so
    // ask for Basic explicitly: with isDigest false, authenticate() above
    // routes to checkBasicAuthentication().
    request->requestAuthentication(nullptr, false);
    return false;
}

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

    // Mount EbookFS without formatting first to preserve user data. If the
    // partition is brand-new/blank, format it once so new boards have storage.
    bool ebookOK = EbookFS.begin(false, "/ebooks", 10, "ebooks");
    if (!ebookOK && partitionLooksBlank(ebooksPart)) {
        Serial.println("EbookFS appears blank; formatting first-use ebook storage...");
        ebookOK = EbookFS.begin(true, "/ebooks", 10, "ebooks");
    }

    if (ebookOK) {
        Serial.printf("EbookFS OK: %u / %u bytes used\n", EbookFS.usedBytes(), EbookFS.totalBytes());
        listFiles(EbookFS, "/", 1);

        // Uploads interrompidos deixam ficheiros .part. Como não são listados
        // por /api/books nem abertos pelo leitor, só ocupam espaço — limpar no
        // arranque, que é o único momento em que nenhum upload está em curso.
        {
            std::vector<String> stale;
            File root = EbookFS.open("/");
            if (root && root.isDirectory()) {
                File f = root.openNextFile();
                while (f) {
                    String n = f.name();
                    if (hasExtensionCI(n, ".part")) stale.push_back(n);
                    f.close();
                    f = root.openNextFile();
                }
                root.close();
            }
            for (const String& n : stale) {
                Serial.printf("A remover upload incompleto: %s\n", n.c_str());
                EbookFS.remove("/" + n);
            }
        }
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

    // mDNS: http://book32.local/send funciona sem saber o IP. Falha
    // silenciosamente em redes que bloqueiam multicast — o IP continua a
    // funcionar, por isso isto nunca é fatal.
    if (MDNS.begin("book32")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://book32.local/");
    } else {
        Serial.println("mDNS: arranque falhou (o IP continua a funcionar)");
    }
}

void WebMgr::stop() {
    if (!_initialized) return;
    MDNS.end();
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

    // Apply a pending reading font-family change from the main loop.
    if (_pendingReaderFontFamily != -1) {
        int fam = _pendingReaderFontFamily;
        _pendingReaderFontFamily = -1;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontFamily(fam);
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

// === Manual book ordering (v1.2.0) ===
// Order persisted in SystemFS /book_order.json as {"order":["a.epub","b.epub"]}.
// Merge rule: files present in the order list come first (in list order);
// files on the FS but absent from the list are appended in FS enumeration order.
static const char* BOOK_ORDER_PATH = "/book_order.json";

static void loadBookOrder(std::vector<String>& order) {
    order.clear();
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_READ);
    if (!f) return;
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["order"].as<JsonArray>();
    for (JsonVariant v : arr) {
        order.push_back(v.as<String>());
    }
}

static void saveBookOrder(const std::vector<String>& order) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("order");
    for (const String& s : order) arr.add(s);
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_WRITE);
    if (f) {
        serializeJson(doc, f);
        f.close();
    }
}

static void removeFromBookOrder(const String& filename) {
    std::vector<String> order;
    loadBookOrder(order);
    bool changed = false;
    for (auto it = order.begin(); it != order.end();) {
        if (*it == filename) { it = order.erase(it); changed = true; }
        else ++it;
    }
    if (changed) saveBookOrder(order);
}

// Merge: ordered entries that still exist first, then the rest in FS order.
// Logic shared with AppReader::scanBooks() via BookOrderLogic.h
// (host test: tools/tests/test_book_order.cpp).
static void applyBookOrder(const std::vector<String>& order, std::vector<String>& fsNames) {
    applyBookOrderT(order, fsNames,
        [](const String& item, const String& key) { return item == key; });
}

// Minimal JSON string escaping for streaming serialization.
static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
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

// Estado de um upload em curso. Partilhado entre o body handler (que decide) e
// o response handler (que reporta). Como o estado é global e o LittleFS é
// single-writer, o endpoint é single-flight: `owner` marca o pedido que detém
// o estado e qualquer outro pedido concorrente é recusado com 409 sem tocar
// no upload activo. `Idle` é o estado "ninguém a usar" — distinto de
// `WriteFailed`, que significa mesmo erro de escrita.
enum class UploadStatus { Idle, Ok, BadExtension, UnsafeName, NoSpace, WriteFailed };

struct UploadState {
    UploadStatus status = UploadStatus::Idle;
    AsyncWebServerRequest* owner = nullptr;
    File file;
    String path;
    String tempPath;
    String finalName;
    String originalName;

    void reset() {
        if (file) file.close();
        status = UploadStatus::Idle;
        owner = nullptr;
        path = "";
        tempPath = "";
        finalName = "";
        originalName = "";
    }
};

static UploadState g_uploadState;

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

        doc["freeSpace"] = EbookFS.totalBytes() - EbookFS.usedBytes();
        doc["totalSpace"] = EbookFS.totalBytes();
        doc["usedSpace"] = EbookFS.usedBytes();
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

    // Página de envio dedicada: caminho curto e memorizável para o atalho no
    // ecrã principal do telemóvel. Sem auth aqui — a página em si não expõe
    // nada; o POST para /api/books/upload é que exige Basic Auth.
    server->on("/send", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SystemFS.exists("/send.html")) {
            request->send(SystemFS, "/send.html", "text/html");
        } else {
            request->send(404, "text/plain", "send.html nao encontrado - correr uploadfs");
        }
    });

    // API: List Books from EbookFS
    // v1.2.0: streamed serialization (no fixed 2KB buffer — previously books
    // beyond the buffer were silently truncated) + manual ordering applied.
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest *request) {
        // 1) Enumerate FS: books (.epub) and fonts (.ttf) separately.
        std::vector<String> epubs, fonts;
        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                if (hasExtensionCI(name, ".epub")) epubs.push_back(name);
                else if (hasExtensionCI(name, ".ttf")) fonts.push_back(name);
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }

        // 2) Apply manual order to books; fonts stay appended in FS order.
        std::vector<String> order;
        loadBookOrder(order);
        applyBookOrder(order, epubs);
        for (const String& f : fonts) epubs.push_back(f);

        // 3) Stream JSON directly — O(1) memory w.r.t. number of books.
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print("{\"books\":[");
        bool first = true;
        for (const String& name : epubs) {
            File f = EbookFS.open("/" + name, FILE_READ);
            size_t sz = f ? f.size() : 0;
            if (f) f.close();
            if (!first) response->print(",");
            first = false;
            response->printf("{\"name\":\"%s\",\"filename\":\"%s\",\"size\":%u}",
                             jsonEscape(getOriginalFilename(name)).c_str(),
                             jsonEscape(name).c_str(),
                             (unsigned)sz);
        }
        response->print("]}");
        request->send(response);
    });

    // API (v1.2.0): Save manual book order. Body: {"order":["a.epub","b.epub"]}
    AsyncCallbackJsonWebHandler* bookOrderHandler = new AsyncCallbackJsonWebHandler("/api/books/order",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            if (!requireAuth(request)) return;
            JsonArray arr = json["order"].as<JsonArray>();
            if (arr.isNull()) {
                request->send(400, "text/plain", "Missing 'order' array");
                return;
            }
            std::vector<String> order;
            for (JsonVariant v : arr) {
                String name = v.as<String>();
                // v1.4.1 (security): the order list is persisted and later
                // used to build paths, so apply the same allow-list.
                if (isSafeBookName(name) && hasExtensionCI(name, ".epub")) {
                    order.push_back(name);
                }
            }
            saveBookOrder(order);
            request->send(200, "application/json", "{\"ok\":true}");
        });
    bookOrderHandler->setMethod(HTTP_POST);
    server->addHandler(bookOrderHandler);

    // API: Upload Book to EbookFS
    //
    // Estado partilhado entre o body handler e o response handler. O
    // ESPAsyncWebServer corre o body handler primeiro, por isso o veredito
    // aqui guardado já está decidido quando a resposta é montada. Uploads são
    // servidos em série (o LittleFS é single-writer), logo `static` é seguro.
    server->on("/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!requireAuth(request)) return;

            if (g_uploadState.owner != request) {
                if (g_uploadState.owner != nullptr) {
                    // Outro upload detém o estado: recusar sem lhe tocar.
                    request->send(409, "application/json",
                        "{\"ok\":false,\"error\":\"outro envio em curso - tenta daqui a pouco\"}");
                } else {
                    // O body handler nunca correu para este pedido (parte
                    // multipart sem filename): não há nada a reportar.
                    request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"pedido sem ficheiro\"}");
                }
                return;
            }

            switch (g_uploadState.status) {
                case UploadStatus::Ok: {
                    String body = "{\"ok\":true,\"name\":\"" +
                                  jsonEscape(g_uploadState.finalName) + "\"}";
                    request->send(200, "application/json", body);
                    break;
                }
                case UploadStatus::BadExtension:
                    request->send(415, "application/json",
                        "{\"ok\":false,\"error\":\"tipo de ficheiro nao suportado\"}");
                    break;
                case UploadStatus::UnsafeName:
                    request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"nome de ficheiro invalido\"}");
                    break;
                case UploadStatus::NoSpace:
                    request->send(507, "application/json",
                        "{\"ok\":false,\"error\":\"sem espaco na particao de ebooks\"}");
                    break;
                case UploadStatus::WriteFailed:
                default:
                    request->send(500, "application/json",
                        "{\"ok\":false,\"error\":\"falha a escrever no armazenamento\"}");
                    break;
            }
            // Libertar o estado: o corpo JSON já foi construído acima. Sem
            // isto o endpoint ficaria trancado para sempre e um POST sem
            // ficheiro herdaria o veredito do upload anterior.
            g_uploadState.reset();
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // v1.5.0: the body handler runs before the response handler, so it
            // must reject unauthenticated uploads itself or data would be
            // written to flash before the 401 is sent.
            if (!request->authenticate(BOOK32_AUTH_USER, WebMgr::devicePassword())) return;

            if (index == 0) {
                // Single-flight: se outro pedido detém o estado, sair sem
                // tocar em nada — nem reset(), que fecharia o File dele.
                if (g_uploadState.owner != nullptr && g_uploadState.owner != request) {
                    return;
                }
                g_uploadState.reset();
                g_uploadState.owner = request;

                // A ligação pode cair a meio do corpo: o `final` nunca chega e
                // o estado ficaria trancado. Este callback também dispara
                // depois de uma resposta normal, mas aí o response handler já
                // limpou o `owner`, por isso a guarda torna-o inofensivo.
                request->onDisconnect([request]() {
                    if (g_uploadState.owner == request) {
                        if (g_uploadState.file) g_uploadState.file.close();
                        if (g_uploadState.tempPath.length()) EbookFS.remove(g_uploadState.tempPath);
                        g_uploadState.reset();
                    }
                });

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

                size_t freeBytes = EbookFS.totalBytes() - EbookFS.usedBytes();
                switch (checkUpload(safeName, request->contentLength(), freeBytes)) {
                    case UploadVerdict::BadExtension:
                        g_uploadState.status = UploadStatus::BadExtension;
                        return;
                    case UploadVerdict::UnsafeName:
                        g_uploadState.status = UploadStatus::UnsafeName;
                        return;
                    case UploadVerdict::NoSpace:
                        g_uploadState.status = UploadStatus::NoSpace;
                        return;
                    case UploadVerdict::Ok:
                        break;
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

                g_uploadState.finalName = safeName;
                g_uploadState.originalName = filename;
                int origSlash = g_uploadState.originalName.lastIndexOf('/');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);
                origSlash = g_uploadState.originalName.lastIndexOf('\\');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);

                g_uploadState.path = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n",
                              g_uploadState.path.c_str(), filename.c_str());
                g_uploadState.tempPath = g_uploadState.path + ".part";
                g_uploadState.file = EbookFS.open(g_uploadState.tempPath, FILE_WRITE);
                if (!g_uploadState.file) {
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                g_uploadState.status = UploadStatus::Ok;
            }

            // Chunks de um pedido que não é o dono são descartados sem tocar
            // no ficheiro. Um chunk depois de um erro também: nada foi aberto.
            if (g_uploadState.owner != request) return;
            if (g_uploadState.status != UploadStatus::Ok) return;

            if (g_uploadState.file && len) {
                if (g_uploadState.file.write(data, len) != len) {
                    Serial.println("Upload: write failed (disco cheio?)");
                    g_uploadState.file.close();
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
            }

            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                if (!EbookFS.rename(g_uploadState.tempPath, g_uploadState.path)) {
                    Serial.println("Upload: rename do .part falhou");
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
            }
        }
    );

    // API: Delete Book from EbookFS
    server->on("/api/books/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "Missing name param");
            return;
        }

        String filename = request->getParam("name")->value();

        // v1.4.1 (security): reject path separators, ".." and unknown
        // extensions before building a filesystem path. Without this,
        // ?name=../spiffs/index.html escaped the ebooks root.
        if (!isSafeBookName(filename)) {
            Serial.printf("Rejected unsafe delete request: %s\n", filename.c_str());
            request->send(400, "text/plain", "Invalid name");
            return;
        }

        String path = "/" + filename;

        if (EbookFS.exists(path)) {
            if (EbookFS.remove(path)) {
                Serial.printf("Deleted: %s\n", path.c_str());
                removeBookMetadata(filename);
                removeBookProgress(filename);
                removeFromBookOrder(filename);
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
        if (!requireAuth(request)) return;
        request->send(200, "text/plain", "Update scheduled - will start in a moment");
        Serial.println("OTA update requested via web UI, scheduling...");
        WebMgr::getInstance()._otaPending = true;
    });

    // API: Reader Settings - GET
    server->on("/api/settings/reader", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);

        ReaderSettings s = SettingsStore::getInstance().loadReader();
        doc["refreshFrequency"] = s.refreshFrequency;
        doc["fontSize"] = s.fontSize;
        doc["fontFamily"] = s.fontFamily;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Reader Settings - POST
    AsyncCallbackJsonWebHandler* readerSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/reader",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            if (!requireAuth(request)) return;

            // Merge into the existing config so one setting doesn't wipe the
            // other. Clamping lives in SettingsStore, shared with the
            // on-device settings menu.
            SettingsStore& store = SettingsStore::getInstance();
            ReaderSettings s = store.loadReader();

            if (json.containsKey("refreshFrequency")) {
                s.refreshFrequency = json["refreshFrequency"].as<int>();
            }
            if (json.containsKey("fontSize")) {
                s.fontSize = SettingsStore::clampFontSize(json["fontSize"].as<int>());
                // Apply live from the main loop if a book is open.
                WebMgr::getInstance()._pendingReaderFontSize = s.fontSize;
            }
            if (json.containsKey("fontFamily")) {
                s.fontFamily = SettingsStore::clampFontFamily(json["fontFamily"].as<int>());
                // Apply live from the main loop if a book is open.
                WebMgr::getInstance()._pendingReaderFontFamily = s.fontFamily;
            }

            if (store.saveReader(s)) {
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

        doc["rotation"] = SettingsStore::getInstance().loadDisplay().rotation;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Display Settings (orientation) - POST. Applies OS-wide; persisted on
    // EbookFS so it survives OTA updates.
    AsyncCallbackJsonWebHandler* displaySettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/display",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            if (!requireAuth(request)) return;
            DisplaySettings s;
            s.rotation = SettingsStore::clampRotation(json["rotation"] | 3);

            if (SettingsStore::getInstance().saveDisplay(s)) {
                // Apply from the main loop (never rotate/draw on the async task).
                WebMgr::getInstance()._pendingRotation = s.rotation;
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
        if (!requireAuth(request)) return;
        if (EbookFS.exists(READER_PROGRESS_PATH)) {
            if (!EbookFS.remove(READER_PROGRESS_PATH)) {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to reset progress\"}");
                return;
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API: Sleep Settings - GET
    server->on("/api/settings/sleep", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        SleepSettings s = SettingsStore::getInstance().loadSleep();
        doc["sleepTimeout"] = s.timeout;
        doc["sleepMessage"] = s.message;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Sleep Settings - POST
    AsyncCallbackJsonWebHandler* sleepSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/sleep",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            if (!requireAuth(request)) return;

            // Merge, so posting only one key doesn't blank the other.
            SettingsStore& store = SettingsStore::getInstance();
            SleepSettings s = store.loadSleep();

            if (json.containsKey("sleepTimeout")) {
                s.timeout = json["sleepTimeout"].as<int>();
            }
            if (json.containsKey("sleepMessage")) {
                s.message = json["sleepMessage"].as<String>();
            }

            if (store.saveSleep(s)) {
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
        if (!requireAuth(request)) return;  // GET by verb, mutating by effect
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

    // === WIFI / HOTSPOT API ENDPOINTS ===

    // API: WiFi status - station connection + hotspot (AP) state
    server->on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;  // leaks home SSID and local IP
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
        if (!requireAuth(request)) return;  // leaks neighbouring networks
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
            if (!requireAuth(request)) return;
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
