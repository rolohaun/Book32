#include "AppPaperboy.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include "GameCore.h"
#include "Book32FS.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "AppMgr.h"
#include "BatteryMgr.h"
#include "WebMgr.h"
#include <esp_timer.h>
#include <WiFi.h>
#include <algorithm>

namespace {
constexpr int GX = (SCREEN_WIDTH - 160) / 2, GY = 170;
void label(Book32Display& d, const String& s, int x, int y, int size = 2) {
    d.setFont(nullptr); d.setTextSize(size); d.setTextColor(GxEPD_BLACK);
    d.setCursor(x, y); d.print(s); d.setTextSize(1);
}
bool inside(int x, int y, int l, int t, int w, int h) {
    return x >= l && y >= t && x < l+w && y < t+h;
}
}

void AppPaperboy::start() {
    WebMgr::getInstance().stop();
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); WiFi.mode(WIFI_OFF);
    BatteryMgr::getInstance().setReaderActive(false);
    BatteryMgr::getInstance().resetIdleTimer();
    InputMgr::getInstance().setCallback([this](InputAction a) {
        if (a == INPUT_SELECT || a == INPUT_BACK) back();
    });
    InputMgr::getInstance().setTouchCallback([this](uint16_t x, uint16_t y) { tap(x, y); });
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    if (!_latest) _latest = (uint8_t*)ps_calloc(160 * 144, 1);
    if (!_snapshot) _snapshot = (uint8_t*)ps_calloc(160 * 144, 1);
    if (!_workFrame) _workFrame = (uint8_t*)ps_calloc(160 * 144, 1);
    if (_mutex && _latest && _snapshot && _workFrame && !_task)
        xTaskCreatePinnedToCore(run, "GameBoy", 8192, this, 1, &_task, 0);
    scan();
    if (!_task) _message = "Not enough memory for emulator";
}

void AppPaperboy::scan() {
    _roms.clear(); _page = 0;
    _message = "Put legal .gb ROMs in /roms on SD";
    if (ebookStorageUsesSD()) {
        File dir = EbookFS.open("/roms");
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                String name = f.name(); name = name.substring(name.lastIndexOf('/') + 1);
                String lower = name; lower.toLowerCase();
                if (!f.isDirectory() && lower.endsWith(".gb") && f.size() >= 0x150 &&
                    f.size() <= 4 * 1024 * 1024 && _roms.size() < 64) _roms.push_back("/roms/" + name);
                f.close();
            }
        }
        if (dir) dir.close();
        std::sort(_roms.begin(), _roms.end());
        if (!_roms.empty()) _message = "Tap a game. BOOT returns to menu.";
    }
    _redraw = true;
}

bool AppPaperboy::save() {
    if (!_mutex || _savePath.isEmpty()) return true;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t size; uint8_t* ram = inkGameRam(&size);
    bool ok = true;
    if (ram && size && inkGameDirty()) {
        String temp = _savePath + ".tmp", backup = _savePath + ".bak";
        File f = EbookFS.open(temp, "w");
        ok = f && f.write(ram, size) == size;
        if (f) { f.flush(); f.close(); }
        if (ok && EbookFS.exists(_savePath)) {
            if (EbookFS.exists(backup)) EbookFS.remove(backup);
            ok = EbookFS.rename(_savePath, backup);
        }
        if (ok) ok = EbookFS.rename(temp, _savePath);
        if (ok) inkGameSaved();
    }
    xSemaphoreGive(_mutex);
    return ok;
}

void AppPaperboy::stop() {
    _playing = false; _buttons = 0;
    if (!save()) Serial.println("Paperboy: save failed; previous .sav/.bak preserved");
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        inkGameClose(); free(_rom); _rom = nullptr;
        xSemaphoreGive(_mutex);
    }
    _savePath = "";
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppPaperboy::back() {
    if (_playing || !_savePath.isEmpty()) {
        _playing = false; _buttons = 0;
        if (!save()) { _message = "Save failed. Check SD; tap Back to retry."; _redraw = true; return; }
        _savePath = "";
        DisplayMgr::getInstance().getDisplay().setGameMode(false);
        _redraw = true;
    } else AppMgr::getInstance().switchTo(0);
}

void AppPaperboy::openBook(const String& path) {
    if (!_task) return;
    if (!_savePath.isEmpty() && !save()) { _message = "Save failed; cannot change games"; _redraw = true; return; }
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.fillScreen(GxEPD_WHITE); label(d, "Loading game...", 30, 180, 3); d.refresh(false);
    File f = EbookFS.open(path, "r");
    const size_t bytes = f ? f.size() : 0;
    if (bytes < 0x150 || bytes > 4 * 1024 * 1024) { _message = "Invalid ROM size"; _redraw = true; return; }
    xSemaphoreTake(_mutex, portMAX_DELAY);
    inkGameClose(); free(_rom); _rom = (uint8_t*)ps_malloc(bytes);
    bool ok = _rom && f.read(_rom, bytes) == bytes;
    f.close();
    if (ok) ok = inkGameOpen(_rom, bytes);
    if (ok) {
        _savePath = path + ".inkdeck.sav";
        size_t size; uint8_t* ram = inkGameRam(&size);
        String source = EbookFS.exists(_savePath) ? _savePath : _savePath + ".bak";
        if (ram && size && EbookFS.exists(source)) {
            File saved = EbookFS.open(source, "r");
            if (!saved || saved.size() != size || saved.read(ram, size) != size) {
                _message = "Invalid save file; left untouched"; ok = false;
            }
            if (saved) saved.close();
        }
        inkGameSaved();
    } else _message = _rom ? inkGameError() : "Not enough ROM memory";
    xSemaphoreGive(_mutex);
    _failed = false; _frameReady = false;
    if (ok) {
        memset(_snapshot, 0, 160 * 144);
        paintGame(); d.refresh(false); d.setGameMode(true);
        _lastClean = _lastStats = millis(); _frames = 0; _updates = 0;
        _playing = true;
    } else { _savePath = ""; _redraw = true; }
}

void AppPaperboy::tap(uint16_t x, uint16_t y) {
    if (y < 85) { back(); return; }
    if (_playing) return; // held/multi-touch controls are sampled separately
    if (y >= 160 && y < 640) {
        unsigned index = _page * 6 + (y - 160) / 80;
        if (index < _roms.size()) openBook(_roms[index]);
    } else if (y >= 700 && y < 780) {
        if (x < 180 && _page) --_page;
        else if (x >= 360 && (_page + 1) * 6 < _roms.size()) ++_page;
        else if (x >= 180 && x < 360) scan();
        _redraw = true;
    }
}

void AppPaperboy::run(void* argument) {
    auto* app = static_cast<AppPaperboy*>(argument);
    uint8_t* frame = app->_workFrame;
    int64_t next = esp_timer_get_time();
    for (;;) {
        if (!app->_playing) { vTaskDelay(pdMS_TO_TICKS(10)); next = esp_timer_get_time(); continue; }
        xSemaphoreTake(app->_mutex, portMAX_DELAY);
        if (app->_playing) {
            if (inkGameFrame(app->_buttons.load(), frame)) {
                memcpy(app->_latest, frame, 160 * 144); app->_frameReady = true; ++app->_frames;
            } else { app->_failed = true; app->_playing = false; }
        }
        xSemaphoreGive(app->_mutex);
        next += 16743;
        int64_t remaining = next - esp_timer_get_time();
        if (remaining < -100000) next = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(remaining > 1000 ? remaining / 1000 : 1));
    }
}

void AppPaperboy::update() {
    if (_failed.exchange(false)) {
        _message = inkGameError();
        DisplayMgr::getInstance().getDisplay().setGameMode(false);
        _redraw = true;
    }
    if (!_playing) return;
    InputMgr::TouchPoint points[5];
    uint8_t count = InputMgr::getInstance().heldTouches(points), buttons = 0;
    for (uint8_t i = 0; i < count; ++i) {
        int x = points[i].x, y = points[i].y;
        if (inside(x,y,25,510,210,210)) {
            if (x < 95) buttons |= 0x20;
            if (x >= 165) buttons |= 0x10;
            if (y < 580) buttons |= 0x40;
            if (y >= 650) buttons |= 0x80;
        }
        if (inside(x,y,395,510,110,110)) buttons |= 1;
        if (inside(x,y,275,625,110,110)) buttons |= 2;
        if (inside(x,y,70,800,160,65)) buttons |= 4;
        if (inside(x,y,290,800,160,65)) buttons |= 8;
    }
    _buttons = buttons;
    if (buttons) BatteryMgr::getInstance().resetIdleTimer();
    if (millis() - _lastStats >= 5000) {
        float seconds = (millis() - _lastStats) / 1000.f;
        Serial.printf("Paperboy: emulator %.1f fps; panel %.1f updates/s\n", _frames.exchange(0)/seconds, _updates/seconds);
        _updates = 0; _lastStats = millis();
    }
}

void AppPaperboy::paintGame() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.fillScreen(GxEPD_WHITE);
    label(d, "< Back / save", 20, 30);
    label(d, "Paperboy - native 160 x 144", 50, 105);
    d.drawRect(GX-5, GY-5, 170,154,GxEPD_BLACK);
    label(d, "Experimental fast refresh", 60, 390);
    d.drawRoundRect(25,580,210,70,8,GxEPD_BLACK);
    d.drawRoundRect(95,510,70,210,8,GxEPD_BLACK);
    label(d,"^",120,535); label(d,"v",120,680); label(d,"<",45,600); label(d,">",195,600);
    d.drawCircle(450,565,53,GxEPD_BLACK); label(d,"A",440,552,3);
    d.drawCircle(330,680,53,GxEPD_BLACK); label(d,"B",320,667,3);
    d.drawRoundRect(70,800,160,65,10,GxEPD_BLACK); label(d,"SELECT",100,823);
    d.drawRoundRect(290,800,160,65,10,GxEPD_BLACK); label(d,"START",330,823);
    label(d,"BOOT: Back    Hold BOOT: Sleep",20,920);
}

void AppPaperboy::draw() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    if (_playing) {
        if (_redraw) { paintGame(); _redraw = false; }
        if (!_frameReady.exchange(false)) return;
        xSemaphoreTake(_mutex, portMAX_DELAY);
        memcpy(_snapshot, _latest, 160 * 144);
        xSemaphoreGive(_mutex);
        const uint8_t threshold[4] = {0,2,3,1};
        for (int y=0; y<144; ++y) for (int x=0; x<160; ++x) {
            uint8_t shade = _snapshot[y*160+x];
            bool black = shade == 3 || (shade && threshold[(y&1)*2+(x&1)] < shade);
            d.drawPixel(GX+x,GY+y,black ? GxEPD_BLACK : GxEPD_WHITE);
        }
        bool clean = millis()-_lastClean >= 30000;
        d.refresh(!clean); ++_updates;
        if (clean) _lastClean=millis();
        return;
    }
    if (!_redraw) return;
    _redraw = false;
    d.setGameMode(false); d.fillScreen(GxEPD_WHITE);
    label(d,"< Back to InkDeck",20,30);
    label(d,"Paperboy / Game Boy",20,100,3);
    for (unsigned row=0; row<6; ++row) {
        unsigned index = _page*6+row;
        if (index >= _roms.size()) break;
        d.drawRoundRect(15,160+row*80,510,70,8,GxEPD_BLACK);
        String name=_roms[index].substring(6); if (name.length()>39) name=name.substring(0,36)+"...";
        label(d,name,25,182+row*80);
    }
    label(d,"Previous",20,720); label(d,"Rescan",215,720); label(d,"Next",420,720);
    label(d,_message,20,810);
    label(d,"Preview: DMG only, no audio or RTC",20,885);
    d.refresh(false);
}
#endif
