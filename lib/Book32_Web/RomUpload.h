#pragma once
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <algorithm>
#include "Book32FS.h"
#include "RomStorage.h"
#include "BatteryMgr.h"
#include "../Apps/AppPaperboy/RomFormat.h"

namespace RomUpload {
static const char* TEMP_PATH = "/roms/.inkdeck-upload.tmp";
// POD: AsyncWebServer frees _tempObject; File belongs to _tempFile.
struct State {
    size_t bytes;
    uint8_t header[512];
    char path[104];
    const char* error;
    int status;
    bool owns, complete, staged;
};
inline void cleanup(AsyncWebServerRequest* request) {
    auto* s = static_cast<State*>(request->_tempObject);
    if (!s || !s->owns) return;
    request->_tempFile.close();
    if (s->staged) EbookFS.remove(TEMP_PATH); // Only this upload's partial file.
    s->staged = false; s->owns = false;
    RomStorage::release(RomStorage::Upload);
}
inline void error(State* s, int status, const char* message) {
    if (!s->error) { s->status = status; s->error = message; }
}
inline void receive(AsyncWebServerRequest* request, String filename, size_t index,
                    uint8_t* data, size_t len, bool final) {
    auto* s = static_cast<State*>(request->_tempObject);
    if (!s) {
        s = static_cast<State*>(calloc(1, sizeof(State)));
        if (!s) return;
        request->_tempObject = s;
        request->onDisconnect([request]() { cleanup(request); });
        if (index) { error(s, 400, "Invalid upload offset"); return; }
        if (!inkRomFilename(filename.c_str())) { error(s, 400, "Choose .gb/.gbc, .nes or .md/.gen/.bin with a simple filename (96 bytes maximum)"); return; }
        if (!ebookStorageUsesSD()) { error(s, 503, "Insert a MicroSD card and restart InkDeck"); return; }
        if (!RomStorage::acquire(RomStorage::Upload)) { error(s, 409, "Another upload or game is active"); return; }
        s->owns = true;
        snprintf(s->path, sizeof(s->path), "/roms/%s", filename.c_str());
        if (!EbookFS.exists("/roms") && !EbookFS.mkdir("/roms")) { error(s, 507, "Cannot create /roms on SD"); return; }
        if (EbookFS.exists(s->path)) { error(s, 409, "A ROM with that name already exists; rename your upload"); return; }
        // The single-upload lease makes this reserved staging path exclusive.
        if (EbookFS.exists(TEMP_PATH) && !EbookFS.remove(TEMP_PATH)) { error(s, 507, "Cannot clear interrupted upload"); return; }
        request->_tempFile = EbookFS.open(TEMP_PATH, "w");
        if (!request->_tempFile) { error(s, 507, "Cannot write to SD card"); return; }
        s->staged = true;
    }
    if (s->error) return;
    if (s->complete || index != s->bytes) { error(s, 400, "Upload one complete ROM at a time"); return; }
    size_t limit=inkRomLimit(inkRomSystem(s->path));
    if (s->bytes>limit || len > limit - s->bytes) { error(s, 413, "ROM exceeds this system's size limit"); return; }
    if (index < sizeof(s->header)) memcpy(s->header+index, data, std::min(len, sizeof(s->header)-index));
    if (len && request->_tempFile.write(data, len) != len) { error(s, 507, "SD write failed or card is full"); return; }
    s->bytes += len;
    BatteryMgr::getInstance().resetIdleTimer();
    if (final) {
        request->_tempFile.flush();
        request->_tempFile.close();
        const char* invalid = inkRomValidateSystem(inkRomSystem(s->path),s->header, s->bytes);
        if (invalid) error(s, 422, invalid);
        else s->complete = true;
    }
}
inline void finish(AsyncWebServerRequest* request) {
    auto* s = static_cast<State*>(request->_tempObject);
    if (!s) { request->send(400, "application/json", "{\"error\":\"No ROM received\"}"); return; }
    if (!s->error && !s->complete) error(s, 400, "Incomplete upload");
    if (!s->error) {
        File check = EbookFS.open(TEMP_PATH, "r");
        bool valid = check && check.size() == s->bytes;
        if (check) check.close();
        if (!valid) error(s, 507, "SD verification failed");
        else if (EbookFS.exists(s->path)) error(s, 409, "ROM already exists");
        else if (!EbookFS.rename(TEMP_PATH, s->path)) error(s, 507, "Cannot finish ROM upload");
        else s->staged = false;
    }
    DynamicJsonDocument doc(384);
    if (s->error) doc["error"] = s->error;
    else { doc["status"] = "ok"; doc["path"] = s->path; doc["size"] = s->bytes; }
    String body; serializeJson(doc, body);
    int status = s->error ? s->status : 201;
    cleanup(request);
    request->send(status, "application/json", body);
}
inline void configure(AsyncWebServer* server) {
    server->on("/api/roms/upload", HTTP_POST, finish, receive);
    server->on("/api/roms/delete", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) { request->send(400, "application/json", "{\"error\":\"Missing ROM filename\"}"); return; }
        String name = request->getParam("name")->value();
        // A basename only, no path traversal, embedded NUL, sidecars or directories.
        if (name.length() != strlen(name.c_str()) || !inkRomFilename(name.c_str())) {
            request->send(400, "application/json", "{\"error\":\"Invalid ROM filename\"}"); return;
        }
        if (!ebookStorageUsesSD()) { request->send(503, "application/json", "{\"error\":\"MicroSD card is not mounted\"}"); return; }
        if (!RomStorage::acquire(RomStorage::Upload)) { request->send(409, "application/json", "{\"error\":\"Another upload or game is active\"}"); return; }
        const String path = "/roms/" + name;
        int status = 200;
        const char* body = "{\"status\":\"ok\",\"savesPreserved\":true}";
        if (!EbookFS.exists(path)) { status = 404; body = "{\"error\":\"ROM not found\"}"; }
        else {
            File file = EbookFS.open(path, "r");
            const bool regular = file && !file.isDirectory();
            if (file) file.close();
            if (!regular) { status = 400; body = "{\"error\":\"Not a readable ROM file\"}"; }
            else if (!EbookFS.remove(path)) { status = 500; body = "{\"error\":\"Could not delete ROM from SD\"}"; }
        }
        // Deliberately never remove .inkdeck.sav, .bak or .rtc sidecars.
        BatteryMgr::getInstance().resetIdleTimer();
        RomStorage::release(RomStorage::Upload);
        request->send(status, "application/json", body);
    });
    server->on("/api/roms", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!ebookStorageUsesSD()) { request->send(503, "application/json", "{\"error\":\"Insert a MicroSD card and restart InkDeck\"}"); return; }
        if (!RomStorage::acquire(RomStorage::Upload)) { request->send(409, "application/json", "{\"error\":\"ROM storage is busy\"}"); return; }
        auto* response = request->beginResponseStream("application/json");
        response->print("{\"roms\":[");
        File dir = EbookFS.open("/roms");
        bool first = true;
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                String name = f.name(); name = name.substring(name.lastIndexOf('/')+1);
                if (!f.isDirectory() && inkRomFilename(name.c_str())) {
                    if (!first) response->print(','); first = false;
                    StaticJsonDocument<256> item;
                    item["name"] = name; item["size"] = f.size();item["system"]=inkSystemName(inkRomSystem(name.c_str()));
                    serializeJson(item, *response);
                }
                f.close();
            }
        }
        if (dir) dir.close();
        response->print("]}");
        RomStorage::release(RomStorage::Upload);
        request->send(response);
    });
}
}
#endif
