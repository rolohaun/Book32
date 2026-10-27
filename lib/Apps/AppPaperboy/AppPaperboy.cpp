#include "AppPaperboy.h"
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY) || defined(BOARD_WAVESHARE_RLCD42)
#include "ConsoleCore.h"
#include "RomFormat.h"
#include "RomStorage.h"
#include "Book32FS.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "AppMgr.h"
#include "BatteryMgr.h"
#include "WebMgr.h"
#include "GameViewport.h"
#include "NesViewport.h"
#include "NesCore.h"
#include "arduboy/ArduboyCore.h"
#include "InkBoyUi.h"
#include "InkBoyLibraryUi.h"
#include <esp_timer.h>
#include <WiFi.h>
#include <algorithm>
#include <soc/soc_memory_types.h>
#include "NetworkWork.h"
#include "GamepadMgr.h"
#include "GameAudio.h"
#include "InkBoyDrive.h"

namespace {
#if INKADE_INKBOY_VIDEO
#define INKBOY_GB_VIDEO_LOG "Inkade 2x: SSD1677 per-pixel drive, 20MHz, window 320x288"
#else
#define INKBOY_GB_VIDEO_LOG "Inkade 3x: fixed Contrast, 6 pulses, 540 rows, 20MHz, window 480x432"
#endif

// Pause / saves screen geometry, shared by painting and hit testing.
struct PauseLayout {
    int left, width;            // full-width buttons
    int resumeY, resumeH;
    int slotY, slotStep, slotH; // four slots
    int saveX, loadX, slotButtonW;
    int exitY, exitH, restartY, restartH;
    int messageY, noteY;
    int confirmTextY, confirmNoteY, confirmY, confirmH, confirmW, confirmRightX;
    int systemLabelX;
};
#if INKADE_INKBOY_VIDEO
constexpr PauseLayout PAUSE = {20, 440, 110, 64, 200, 82, 64, 210, 340, 120,
                               560, 64, 640, 60, 718, 748, 250, 300, 420, 76, 210, 250, 330};
#else
constexpr PauseLayout PAUSE = {20, 480, 125, 70, 240, 90, 70, 225, 370, 130,
                               650, 75, 750, 70, 850, 900, 280, 330, 460, 80, 225, 275, 350};
#endif

void label(Book32Display& d, const String& s, int x, int y, int size = 2) {
    d.setFont(nullptr); d.setTextSize(size); d.setTextColor(GxEPD_BLACK);
    d.setCursor(x, y); d.print(s); d.setTextSize(1);
}
bool inside(int x, int y, int l, int t, int w, int h) {
    return x >= l && y >= t && x < l+w && y < t+h;
}
uint8_t* allocateFrame(size_t bytes) {
    // Library-entry scratch buffers precede the persistent task stack and ROM
    // list allocations. Putting them in internal RAM splits its large block;
    // keep the internal heap available for the display worker and history.
    // openBook promotes these to internal RAM for GB only, after selection.
    uint8_t* frame = (uint8_t*)ps_calloc(bytes, 1);
    return frame ? frame : (uint8_t*)heap_caps_calloc(bytes, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
// Verify a complete temporary file before replacing the previous slot. The
// .bak also provides recovery if power is lost between the two FAT renames.
bool writeStateFile(const String& path, const uint8_t* data, size_t bytes) {
    String temp = path + ".tmp", backup = path + ".bak";
    File f = EbookFS.open(temp, "w");
    bool ok = f && f.write(data, bytes) == bytes;
    if (f) { f.flush(); f.close(); }
    if (!ok) return false;
    f = EbookFS.open(temp, "r");
    ok = f && f.size() == bytes;
    uint8_t check[512];
    for (size_t offset = 0; ok && offset < bytes; offset += sizeof(check)) {
        size_t count = std::min(sizeof(check), bytes - offset);
        ok = f.read(check, count) == count && memcmp(check, data + offset, count) == 0;
    }
    if (f) f.close();
    if (!ok) return false;
    bool previous = EbookFS.exists(path);
    if (previous) {
        if (EbookFS.exists(backup) && !EbookFS.remove(backup)) return false;
        if (!EbookFS.rename(path, backup)) return false;
    }
    if (EbookFS.rename(temp, path)) return true;
    if (previous) EbookFS.rename(backup, path);
    return false;
}
}

void AppPaperboy::start() {
    _storageOwned = RomStorage::acquire(RomStorage::Game);
    if (!_storageOwned) {
        _roms.clear(); _filtered.clear(); _systemMask=0; _message = "Upload busy. Go back and try again."; _redraw = true;
        InputMgr::getInstance().setCallback([this](InputAction a) { if (a == INPUT_SELECT || a == INPUT_BACK) back(); });
        InputMgr::getInstance().setTouchCallback([this](uint16_t x, uint16_t y) { if (y < 60) back(); });
        return;
    }
    NetworkWork::gate().pause();
    if (!NetworkWork::gate().idle()) {
        auto& display=DisplayMgr::getInstance().getDisplay();
        display.setFullWindow(); display.fillScreen(GxEPD_WHITE);
        label(display,"Preparing Inkade",BOOK32_COMPACT_UI ? 12 : 30,180,BOOK32_COMPACT_UI ? 2 : 3);
        label(display,"Finishing network activity...",30,250,2);
        display.refresh(false);
        Serial.println("Inkade: waiting for network owners before radio shutdown");
        while (!NetworkWork::gate().idle()) vTaskDelay(pdMS_TO_TICKS(10));
    }
    WebMgr::getInstance().stop();
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); WiFi.mode(WIFI_OFF);
    BatteryMgr::getInstance().setReaderActive(false);
    BatteryMgr::getInstance().resetIdleTimer();
    InputMgr::getInstance().setCallback([this](InputAction a) { keyAction(a); });
    InputMgr::getInstance().setTouchCallback([this](uint16_t x, uint16_t y) { tap(x, y); });
    loadPadMap();
    _controls = false; _capture = -1; _captureRelease = false;
#if INKADE_INKBOY_VIDEO
    _screen = SCREEN_LIBRARY;
    _display = InkadeSettings::Active();
    InkadeSettings::load(_display);  // defaults when the card has none
    DisplayMgr::getInstance().getDisplay().setGameSettings(_display.video);
    InputMgr::getInstance().setSerialHandler([this](const char* line) { serialSetting(line); });
#endif
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    if (!_frameMutex) _frameMutex = xSemaphoreCreateMutex();
    if (!_latest) _latest = allocateFrame(GameViewport::PACKED_BYTES);
    if (!_snapshot) _snapshot = allocateFrame(GameViewport::PACKED_BYTES);
    if (!_packedFrame) _packedFrame = allocateFrame(GameViewport::PACKED_BYTES);
    if (!_workFrame) _workFrame = allocateFrame(160 * 144);
    if (_mutex && _frameMutex && _latest && _snapshot && _packedFrame && _workFrame && !_task)
        xTaskCreatePinnedToCore(run, "InkBoy", 12288, this, 1, &_task, 0);
    scan();
    if (!_task || !_latest || !_snapshot || !_packedFrame || !_workFrame) _message = "Not enough memory for emulator";
}

void AppPaperboy::scan() {
    _roms.clear(); _page = 0; _systemMask=0;
    _message = "Add Game Boy or NES ROMs to /roms on SD.";
    if (romStorageReady()) {
        File dir = EbookFS.open("/roms");
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                String name = f.name(); name = name.substring(name.lastIndexOf('/') + 1);
                InkSystem system=inkRomSystem(name.c_str());
#if INKADE_INKBOY_VIDEO
                // NES (256x240) does not fit the Sticky's 2x video window yet.
                if (system == INK_SYSTEM_NES) { f.close(); continue; }
#endif
                if (!f.isDirectory() && inkRomFilename(name.c_str()) && inkRomSizeValid(system,f.size()) && _roms.size() < 256) {
                    _roms.push_back("/roms/" + name); _systemMask|=1<<system;
                }
                f.close();
            }
        }
        if (dir) dir.close();
        std::sort(_roms.begin(), _roms.end(), [](const String& a,const String& b){ return strcasecmp(a.c_str(),b.c_str())<0; });
#if defined(BOARD_WAVESHARE_RLCD42)
        if (!_roms.empty()) _message = "Hold KEY on a game to play or resume it.";
#else
        if (!_roms.empty()) _message = "Tap a game to play or resume. Saves stay with each ROM.";
#endif
    }
#if defined(BOARD_WAVESHARE_RLCD42)
    // Built-in Arduboy games head the list, with or without an SD card.
    for (unsigned i = 0; i < inkArduboyCount(); ++i) _roms.insert(_roms.begin() + i, inkArduboyPath(i));
#endif
    if(!(_systemMask&(1<<_selectedSystem))) {
        _selectedSystem=INK_SYSTEM_NONE;
        for(int i=INK_SYSTEM_GB;i<=INK_SYSTEM_NES;i++)if(_systemMask&(1<<i)){_selectedSystem=(InkSystem)i;break;}
    }
    filterRoms();
    _redraw = true;
}
void AppPaperboy::filterRoms() {
    _filtered.clear();_page=0;_cursor=0;
#if defined(BOARD_WAVESHARE_RLCD42)
    // One list of every system: there are no tabs to tap.
    for(unsigned i=0;i<_roms.size();i++)_filtered.push_back(i);
#else
    for(unsigned i=0;i<_roms.size();i++)if(inkRomSystem(_roms[i].c_str())==_selectedSystem)_filtered.push_back(i);
#endif
}

size_t AppPaperboy::packedBytes() const {
    return _nesVideo ? NesViewport::PACKED_BYTES : GameViewport::PACKED_BYTES;
}
bool AppPaperboy::configureVideo(bool nes) {
    if (_nesVideo==nes) return true;
    const size_t bytes=nes ? NesViewport::PACKED_BYTES : GameViewport::PACKED_BYTES;
    uint8_t* latest=allocateFrame(bytes), *snapshot=allocateFrame(bytes), *packed=allocateFrame(bytes);
    if(!latest || !snapshot || !packed) { free(latest);free(snapshot);free(packed);return false; }
    // Called stopped, under the core lock, with the display's borrowed pointer
    // already released. Commit all three allocations together (or none).
    free(_latest);free(_snapshot);free(_packedFrame);
    _latest=latest;_snapshot=snapshot;_packedFrame=packed;
    _nesVideo=nes;_frameReady=false;_snapshotValid=false;
    return true;
}
void AppPaperboy::packVideo(uint8_t* destination) {
    if (_nesVideo) {
        unsigned pitch;const uint8_t* palette;
        const uint8_t* indexed=inkNesIndexedImage(&pitch,&palette);
        if(indexed) NesViewport::packIndexed(destination,indexed,pitch,palette,_gameRotation);
        else NesViewport::pack2x(destination,inkNesNativeImage(),_gameRotation);
    }
    else {
#if INKADE_INKBOY_VIDEO
        // InkBoy drive input: the 2-bit source (grayscale) or the Bayer-
        // dithered window (dithered).
        if (_display.video.showFps) InkBoyDrive::stampFps(_workFrame,_fpsShown.load());
        if (_display.video.mode == InkBoyDrive::MODE_GRAYSCALE)
            InkBoyDrive::packSource(destination,_workFrame,_gameRotation);
        else
            InkBoyDrive::packDithered(destination,_workFrame,_gameRotation);
#else
        GameViewport::pack(destination,_workFrame,_gameRotation);
#endif
    }
}

// Packed-frame byte that shows an all-white picture.
uint8_t AppPaperboy::whiteFill() const {
    if (_nesVideo) return 0;  // NES shade 0
#if defined(BOARD_WAVESHARE_RLCD42)
    return 0;  // Game Boy shade 0: the RLCD frame is unpacked shades
#endif
#if INKADE_INKBOY_VIDEO
    if (_display.video.mode == InkBoyDrive::MODE_GRAYSCALE) return 0;  // 2-bit shade 0
#endif
    return 0xff;  // GB: bit 1 is white
}

// LILYGO: scans start at least 16.7 ms apart, as fast as the panel goes.
void AppPaperboy::waitForScanSlot(bool clean) {
    if (clean || !_lastScanStarted) return;
    const int32_t elapsed = int32_t(micros() - _lastScanStarted);
    if (elapsed < 16667) delayMicroseconds(16667 - elapsed);
}

void AppPaperboy::scanStarted(bool) { _lastScanStarted = micros(); }

void AppPaperboy::countFps(bool fresh) {
    if (fresh) ++_fpsCount;
    const unsigned long now = millis();
    if (now - _fpsAt >= 1000) {
        _fpsShown = uint8_t(std::min<unsigned long>(99, _fpsCount * 1000UL / (now - _fpsAt)));
        _fpsCount = 0; _fpsAt = now;
#if INKADE_INKBOY_VIDEO
        if (_screen == SCREEN_TEST) Serial.printf("Inkade test: %u fps\n", unsigned(_fpsShown.load()));
#endif
    }
}

bool AppPaperboy::save() {
    if (!_mutex || _savePath.isEmpty()) return true;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t size; uint8_t* ram = inkConsoleRam(&size);
    bool ok = true;
    if (ram && size && inkConsoleDirty()) {
        String temp = _savePath + ".tmp", backup = _savePath + ".bak";
        File f = EbookFS.open(temp, "w");
        ok = f && f.write(ram, size) == size;
        if (f) { f.flush(); f.close(); }
        if (ok && EbookFS.exists(_savePath)) {
            if (EbookFS.exists(backup)) EbookFS.remove(backup);
            ok = EbookFS.rename(_savePath, backup);
        }
        if (ok) ok = EbookFS.rename(temp, _savePath);
        if (ok) inkConsoleSaved();
    }
    if (ok && inkConsoleHasRtc()) {
        uint8_t state[INK_RTC_STATE_BYTES];
        if (inkConsoleRtcExport(state)) {
            String path = _savePath + ".rtc", temp = path + ".tmp", backup = path + ".bak";
            File f = EbookFS.open(temp, "w");
            ok = f && f.write(state, sizeof(state)) == sizeof(state);
            if (f) { f.flush(); f.close(); }
            if (ok && EbookFS.exists(path)) {
                if (EbookFS.exists(backup)) EbookFS.remove(backup);
                ok = EbookFS.rename(path, backup);
            }
            if (ok) ok = EbookFS.rename(temp, path);
        }
    }
    xSemaphoreGive(_mutex);
    return ok;
}

bool AppPaperboy::saveState(unsigned slot) {
    // Arduboy games have no save states, only their EEPROM (save()).
    if (inkConsoleSystem() == INK_SYSTEM_ARDUBOY) return slot == 0;
    if (!_mutex || _savePath.isEmpty() || slot > 3 || _coreFault) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t size = inkConsoleStateSize();
    uint8_t* data = size ? (uint8_t*)ps_malloc(size) : nullptr;
    bool ok = data && inkConsoleStateExport(data, size);
    xSemaphoreGive(_mutex);
    if (ok) ok = writeStateFile(_savePath + ".state" + String(slot), data, size);
    free(data);
    if (ok) _slots[slot] = true;
    _message = ok ? (slot ? "Saved to slot " + String(slot) : "Automatic resume saved") : "Save failed; check SD card";
    Serial.printf("Inkade state %u: %s\n", slot, _message.c_str());
    return ok;
}

bool AppPaperboy::loadState(unsigned slot) {
    if (!_mutex || _savePath.isEmpty() || slot > 3) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t size = inkConsoleStateSize();
    uint8_t* data = size ? (uint8_t*)ps_malloc(size) : nullptr;
    String path = _savePath + ".state" + String(slot);
    if (!EbookFS.exists(path)) path += ".bak";
    File f = EbookFS.open(path, "r");
    bool ok = data && f && f.size() == size && f.read(data, size) == size;
    if (f) f.close();
    _message = "Unreadable or incompatible save";
    if (ok) {
        ok = inkConsoleStateImport(data, size);
        if (!ok) _message = inkConsoleError();
    }
    free(data);
    if (ok) {
        inkConsoleImage(_workFrame);
        packVideo(_snapshot);
        _snapshotValid = true;
        _frameReady = false; // Discard a pending frame from BEFORE the load.
        _snapshotSequence = _producedSequence;
        _coreFault = false; _failed = false;
        _message = slot ? "Loaded slot " + String(slot) : "Resumed automatic save";
    }
    xSemaphoreGive(_mutex);
    Serial.printf("Inkade load %u: %s\n", slot, _message.c_str());
    return ok;
}

void AppPaperboy::inspectSlots() {
    for (unsigned i = 0; i < 4; ++i) {
        String path = _savePath + ".state" + String(i);
        _slots[i] = EbookFS.exists(path) || EbookFS.exists(path + ".bak");
    }
}
void AppPaperboy::pauseGame() {
    _playing = false; _buttons = 0;
    GameAudio::getInstance().stop();
    if (_task) xTaskNotifyGive(_task);
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    xSemaphoreTake(_mutex, portMAX_DELAY); // Wait for a complete core frame.
    xSemaphoreGive(_mutex);
    _paused = true; _confirmation = 0; _redraw = true;
#if defined(BOARD_WAVESHARE_RLCD42)
    _menuCursor = 0;
#endif
    inspectSlots();
}
void AppPaperboy::resumeGame() {
    if (_coreFault) { _message = "Load a save or restart the game"; _redraw = true; return; }
    auto& d = DisplayMgr::getInstance().getDisplay();
    _paused = false; _confirmation = 0;
    _redraw = false; _forceClean = true; _lastScanStarted = 0;
    paintGame(); d.refresh(false);
#if INKADE_INKBOY_VIDEO
    d.setGameSettings(_display.video);
    _nextImage = 0; _lastChange = esp_timer_get_time(); _analogOn = true;
#endif
    d.setGameMode(true);
    _fpsCount = 0; _fpsAt = millis();
    _lastStats = millis(); _frames = 0; _drawnFrames = 0; _updates = 0;
    _emulationUs = _packingUs = _overBudgetFrames = 0;
    _renderUs = _refreshUs = _regularUpdates = 0;
    _presentations.clearCounts();
    resetFrameTiming(); _playing = true;
    GameAudio::getInstance().start();
    xTaskNotifyGive(_task);
}
void AppPaperboy::exitGame() {
    if (!_coreFault && (!saveState(0) || !save())) { _redraw = true; return; }
    xSemaphoreTake(_mutex,portMAX_DELAY);
    inkConsoleClose(); free(_rom); _rom=nullptr;
    if(_romFile)_romFile.close();
    xSemaphoreGive(_mutex);
    _savePath = ""; _paused = false; _confirmation = 0; _redraw = true;
    _message = _coreFault ? "Game stopped. Previous saves kept." : "Game saved. Tap it to resume.";
}

void AppPaperboy::stop() {
    if (!_storageOwned) {
        InputMgr::getInstance().clearCallback(); InputMgr::getInstance().clearTouchCallback(); return;
    }
    _playing = false; _buttons = 0;
    GameAudio::getInstance().stop();
    if (_task) xTaskNotifyGive(_task); // Never leave a producer waiting on exit.
    // Neutralize pending video drive before SD saves or other blocking work.
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    if (!_savePath.isEmpty() && !_coreFault && (!saveState(0) || !save()))
        Serial.println("Inkade: save failed; previous saves preserved");
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        inkConsoleClose(); free(_rom); _rom = nullptr;
        if (_romFile) _romFile.close();
        // Emulation has stopped and this lock also waits for the last publish.
        // Return internal frame memory before Wi-Fi and the web UI restart.
        free(_latest); free(_snapshot); free(_workFrame); free(_packedFrame);
        _latest = _snapshot = _workFrame = _packedFrame = nullptr;
        _nesVideo = false;
        _frameReady = false;
        xSemaphoreGive(_mutex);
    }
    _savePath = "";
    _paused = false; _confirmation = 0;
    GamepadMgr::getInstance().setRawMode(false);
#if INKADE_INKBOY_VIDEO
    _screen = SCREEN_LIBRARY;
    InputMgr::getInstance().clearSerialHandler();
#endif
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
#if defined(BOARD_WAVESHARE_RLCD42)
    useLandscape(false);  // Hand the next app the portrait screen
#endif
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
    _storageOwned = false;
    RomStorage::release(RomStorage::Game);
    NetworkWork::gate().resume();
}

// Keys: the board's buttons and a Bluetooth gamepad's menu actions.
void AppPaperboy::keyAction(InputAction a) {
#if defined(BOARD_WAVESHARE_RLCD42)
    rlcdKey(a);
    return;
#endif
    if (_playing) {  // the pad itself is read raw while playing
        if (a == INPUT_SELECT || a == INPUT_BACK) back();
        return;
    }
    if (_controls) { controlsKey(a); return; }
#if INKADE_INKBOY_VIDEO
    if (_screen != SCREEN_LIBRARY) {
        if (a == INPUT_BACK) back();
        return;
    }
#endif
    if (_paused) {
        // Select resumes; Back saves and returns to the game list.
        if (a == INPUT_SELECT || (a == INPUT_BACK && _confirmation)) back();
        else if (a == INPUT_BACK) exitGame();
        return;
    }
    if (a == INPUT_NEXT || a == INPUT_PREV) {
        if (_filtered.empty()) return;
        if (_cursorShown) {
            const int n = _filtered.size();
            _cursor = (_cursor + (a == INPUT_NEXT ? 1 : n - 1)) % n;
        }
        _cursorShown = true;
        _page = _cursor / InkBoyLibraryUi::ROWS;
        _redraw = true;
    } else if (a == INPUT_SELECT) {
        if (_cursorShown && _cursor < _filtered.size()) openBook(_roms[_filtered[_cursor]]);
        else if (!_filtered.empty()) { _cursorShown = true; _redraw = true; }
    } else if (a == INPUT_BACK) {
        back();
    }
}

void AppPaperboy::back() {
    if (_controls) { controlsBack(); return; }
#if INKADE_INKBOY_VIDEO
    if (_screen == SCREEN_TEST) { stopTest(); return; }
    if (_screen == SCREEN_SETTINGS) {
        if (_settingsPage != PAGE_MAIN) _settingsPage = PAGE_MAIN;
        else _screen = SCREEN_LIBRARY;
        _redraw = true;
        return;
    }
#endif
    if (_playing) { _message = ""; pauseGame(); }
    else if (_paused && _confirmation) { _confirmation = 0; _redraw = true; }
    else if (_paused) resumeGame();
    else AppMgr::getInstance().switchTo(0);
}

void AppPaperboy::openBook(const String& path) {
    if (!_task || !_latest || !_snapshot || !_packedFrame || !_workFrame) return;
    if (!_savePath.isEmpty() && !save()) { _message = "Save failed; cannot change games"; _redraw = true; return; }
    auto& d = DisplayMgr::getInstance().getDisplay();
    InkSystem selected=inkRomSystem(path.c_str());
#if BOOK32_COMPACT_UI
    useLandscape(false);
    constexpr int LEFT = 12, BIG = 2;
    constexpr unsigned TITLE_CHARS = 23;
#else
    constexpr int LEFT = 30, BIG = 3;
    constexpr unsigned TITLE_CHARS = 38;
#endif
    d.setFullWindow(); d.fillScreen(GxEPD_WHITE); label(d,"Inkade",LEFT,100,4);
    label(d,"Opening your game",LEFT,205,BIG);
    label(d,String(inkSystemName(selected))+" emulator",LEFT,264,2);
    String title=path.substring(path.lastIndexOf('/')+1);if(title.length()>TITLE_CHARS)title=title.substring(0,TITLE_CHARS-3)+"...";
    label(d,title,LEFT,310,2);label(d,"Loading ROM and saved progress...",LEFT,360,1);d.refresh(false);
    // Built-in Arduboy games have no ROM file: `bytes` is the game's index.
    const int builtIn = selected == INK_SYSTEM_ARDUBOY ? inkArduboyFind(path.c_str()) : -1;
    File f = builtIn < 0 ? EbookFS.open(path, "r") : File();
    const size_t bytes = builtIn >= 0 ? size_t(builtIn) : f ? f.size() : 0;
    if (builtIn < 0 && !inkRomSizeValid(selected,bytes)) { _message = "Invalid ROM size for this system"; _redraw = true; return; }
    xSemaphoreTake(_mutex, portMAX_DELAY);
    inkConsoleClose(); free(_rom); _rom = nullptr;
    // No renderer may retain a borrowed frame while changing its allocation.
    // Keep the larger NES allocations in PSRAM, leaving internal RAM
    // available for display DMA. GB retains its internal-first preference.
    d.setGameMode(false);
    if(!configureVideo(selected==INK_SYSTEM_NES)) {
        if (_romFile) _romFile.close();
        f.close();_savePath="";_message="Not enough NES video memory";_redraw=true;
        xSemaphoreGive(_mutex);return;
    }
    d.setNesGame(_nesVideo);
#if defined(BOARD_WAVESHARE_RLCD42)
    d.setArduboyGame(selected == INK_SYSTEM_ARDUBOY);
#endif
    auto placeFrame = [selected](uint8_t*& frame, size_t size) {
        const bool wantInternal=selected==INK_SYSTEM_GB;
        if (esp_ptr_internal(frame)==wantInternal) return;
        uint8_t* replacement=wantInternal ?
            (uint8_t*)heap_caps_calloc(size,1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT) :
            (uint8_t*)ps_calloc(size,1);
        if (replacement) { free(frame);frame=replacement; }
    };
    placeFrame(_latest,packedBytes());
    placeFrame(_snapshot,packedBytes());
    placeFrame(_packedFrame,packedBytes());
    placeFrame(_workFrame,160*144);
    if (_romFile) _romFile.close();
    bool banked = selected==INK_SYSTEM_GB && bytes > 4U * 1024U * 1024U;
    size_t loaded = banked ? 16384 : bytes;
    _rom = builtIn < 0 ? (uint8_t*)ps_malloc(loaded) : nullptr;
    bool readOk = builtIn >= 0 || (_rom && f.read(_rom, loaded) == loaded);
    bool ok = readOk;
    if (ok && banked) {
        _romFile = f;
        ok = inkConsoleOpen(selected,_rom, bytes, [](void* context, size_t offset, uint8_t* out, size_t count) {
            File* file = static_cast<File*>(context);
            return file->seek(offset) && file->read(out, count) == count;
        }, &_romFile);
    } else {
        if (f) f.close();
        if (ok) ok = inkConsoleOpen(selected,_rom, bytes,nullptr,nullptr);
    }
    if (ok) {
        // A built-in game saves its EEPROM to /roms when there is a card.
        if (builtIn >= 0 && romStorageReady() && !EbookFS.exists("/roms")) EbookFS.mkdir("/roms");
        _savePath = builtIn >= 0 && !romStorageReady() ? String() : path + ".inkdeck.sav";
        size_t size; uint8_t* ram = inkConsoleRam(&size);
        String source = EbookFS.exists(_savePath) ? _savePath : _savePath + ".bak";
        if (ram && size && EbookFS.exists(source)) {
            File saved = EbookFS.open(source, "r");
            if (!saved || saved.size() != size || saved.read(ram, size) != size) {
                _message = "Invalid save file; left untouched"; ok = false;
            }
            if (saved) saved.close();
        }
        if (ok && inkConsoleHasRtc()) {
            String rtcPath = _savePath + ".rtc";
            String source = EbookFS.exists(rtcPath) ? rtcPath : rtcPath + ".bak";
            if (EbookFS.exists(source)) {
                uint8_t state[INK_RTC_STATE_BYTES]; File rtc = EbookFS.open(source, "r");
                if (!rtc || rtc.size() != sizeof(state) || rtc.read(state, sizeof(state)) != sizeof(state) || !inkConsoleRtcImport(state)) {
                    _message = "Invalid RTC save; left untouched"; ok = false;
                }
                if (rtc) rtc.close();
            }
        }
        inkConsoleSaved();
    } else _message = !_rom && builtIn < 0 ? "Not enough ROM memory" : !readOk ? "Could not read ROM from SD" : inkConsoleError();
    _producedSequence = _latestSequence = _snapshotSequence = 0;
    _snapshotValid = false;
    xSemaphoreGive(_mutex);
    _failed = false; _frameReady = false;
    if (ok) {
        _gameRotation = d.getRotation(); // Stable until gameplay stops.
        _gamePeriodUs=inkConsolePeriod();
        memset(_snapshot, whiteFill(), packedBytes()); // NES shade 0 / GB bit 1 are white.
        _coreFault = false; _paused = false; _confirmation = 0;
        inspectSlots();
        bool autoFailed = _slots[0] && !loadState(0);
        _videoEnabled = true;
        _redraw = false;
        _forceClean = true; // Establish the first game frame at full contrast.
        _renderUs = _refreshUs = _regularUpdates = 0;
        _emulationUs = _packingUs = _overBudgetFrames = 0;
        _lastScanStarted = 0;
        _presentations.reset();
        resetFrameTiming();
        Serial.println(_nesVideo ? "Inkade NES 2x: native 256x240, window 512x480, four shades, fixed Contrast" :
                                  INKBOY_GB_VIDEO_LOG);
        Serial.printf("Game core: %s; %u us/frame (grayscale, %s); CPU %u MHz\n",inkConsoleName(),_gamePeriodUs,
                      GameAudio::getInstance().isSupported() ? "sound" : "no audio",getCpuFrequencyMhz());
        _lastClean = _lastStats = millis(); _frames = 0; _drawnFrames = 0; _updates = 0;
        if (autoFailed) { _paused = true; _redraw = true; }
        else resumeGame();
    } else {
        Serial.printf("Inkade load failed: %s\n", _message.c_str());
        xSemaphoreTake(_mutex, portMAX_DELAY);
        inkConsoleClose(); free(_rom); _rom = nullptr;
        if (_romFile) _romFile.close();
        xSemaphoreGive(_mutex);
        _savePath = ""; _redraw = true;
    }
}

void AppPaperboy::tap(uint16_t x, uint16_t y) {
    if (_controls) { controlsTap(x, y); return; }
#if INKADE_INKBOY_VIDEO
    if (_screen == SCREEN_SETTINGS) { settingsTap(x, y); return; }
    if (_screen == SCREEN_TEST) { testTap(x, y); return; }
    if (!_playing && !_paused && y < 60 && x >= InkBoyLibraryUi::SETTINGS_X) { openSettings(); return; }
#endif
    if (!_playing && !_paused && InkBoyLibraryUi::controlsAt(x, y)) { openControls(); return; }
    if (y < (_playing && _nesVideo ? NesViewport::Y : 60)) { back(); return; }
    if (_paused) {
        const PauseLayout& p = PAUSE;
        if (_confirmation) {
            if (inside(x,y,p.left,p.confirmY,p.confirmW,p.confirmH)) { _confirmation = 0; _redraw = true; }
            else if (inside(x,y,p.confirmRightX,p.confirmY,p.confirmW,p.confirmH)) {
                uint8_t action = _confirmation; _confirmation = 0;
                if (action == 1) saveState(_selectedSlot);
                else if (action == 2) { if (loadState(_selectedSlot)) { resumeGame(); return; } }
                else {
                    xSemaphoreTake(_mutex, portMAX_DELAY);
                    bool ok = inkConsoleRestart();
                    _frameReady = false; _snapshotValid = false;
                    memset(_snapshot, whiteFill(), packedBytes());
                    xSemaphoreGive(_mutex);
                    _coreFault = !ok;
                    if (ok) { resumeGame(); return; }
                    _message = inkConsoleError();
                }
                _redraw = true;
            }
        } else if (inside(x,y,p.left,p.resumeY,p.width,p.resumeH)) resumeGame();
        else if (y >= p.slotY && y < p.slotY + 4 * p.slotStep) {
            unsigned slot = (y - p.slotY) / p.slotStep;
            if ((y - p.slotY) % p.slotStep < p.slotH) {
                _selectedSlot = slot;
                if (x >= p.loadX && x < p.loadX + p.slotButtonW && _slots[slot]) _confirmation = 2;
                else if (x >= p.saveX && x < p.saveX + p.slotButtonW && slot && !_coreFault) {
                    if (_slots[slot]) _confirmation = 1; else saveState(slot);
                }
                _redraw = true;
            }
        } else if (inside(x,y,p.left,p.exitY,p.width,p.exitH)) { exitGame(); }
        else if (inside(x,y,p.left,p.restartY,p.width,p.restartH)) { _confirmation = 3; _redraw = true; }
        return;
    }
    if (_playing) {
        // A manual recovery for accumulated ghosting, without clearing the
        // touch controls or restarting emulation. Page/game taps aren't keys.
        if (_nesVideo ? inside(x,y,NesViewport::X,NesViewport::Y,NesViewport::W,NesViewport::H) :
                        inside(x,y,GameViewport::X,GameViewport::Y,GameViewport::W,GameViewport::H)) {
            _forceClean = true;
            return;
        }
        return; // held/multi-touch game controls are sampled separately
    }
    InkSystem tab=InkBoyLibraryUi::tabAt(_systemMask,x,y);
    if(tab!=INK_SYSTEM_NONE){if(tab!=_selectedSystem){_selectedSystem=tab;filterRoms();_redraw=true;}return;}
    int row=InkBoyLibraryUi::rowAt(x,y);
    if(row>=0) {
        unsigned index=_page*InkBoyLibraryUi::ROWS+row;
        if(index<_filtered.size())openBook(_roms[_filtered[index]]);
    } else {
        const int nav = InkBoyLibraryUi::navAt(x,y);
        if (nav < 0) return;
        if (nav==0&&_page) --_page;
        else if (nav==2&&(_page+1)*InkBoyLibraryUi::ROWS<_filtered.size()) ++_page;
        else if (nav==1) scan();
        _redraw = true;
    }
}

void AppPaperboy::resetFrameTiming() {
    // Opening a game or entering the safety fallback waits only for current
    // emulation, never for a VSYNC while holding this mutex.
    xSemaphoreTake(_mutex, portMAX_DELAY);
#if INKADE_INKBOY_VIDEO
    // InkBoy: the emulator keeps its own 59.7 fps clock; the display samples
    // the newest finished frame on its 33.3 ms image clock.
    _scanSynchronized = false;
#else
    _scanSynchronized = _videoEnabled;
#endif
    _frameClock.reset(_producedSequence,_gamePeriodUs);
    _requestedFrame = _frameClock.target();
    xSemaphoreGive(_mutex);
    if (_task) xTaskNotifyGive(_task);
}

void AppPaperboy::requestNextFrame(bool maintenance) {
    if (!_videoEnabled || !_playing) return;
    _requestedFrame = _frameClock.boundary(esp_timer_get_time(), maintenance);
    xTaskNotifyGive(_task);
}

void AppPaperboy::run(void* argument) {
    auto* app = static_cast<AppPaperboy*>(argument);
    int64_t next = esp_timer_get_time();
    bool previousSync = false;
    for (;;) {
        if (!app->_playing || app->_scanSynchronized) {
            // Coalesce old boundaries; always read the newest cumulative target.
            // Timeout is only for lifecycle checks, not an independent frame clock.
            if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50))) continue;
        }
        if (!app->_playing) { next = esp_timer_get_time(); continue; }
        xSemaphoreTake(app->_mutex, portMAX_DELAY);
        const bool synchronized = app->_scanSynchronized;
        if (synchronized != previousSync) next = esp_timer_get_time();
        previousSync = synchronized;
        // Bound each work batch so stop/save can acquire the core lock promptly.
        // Catch-up executes every core frame; only presentation may skip frames.
        const uint32_t due = synchronized ?
            std::min(uint32_t(2), app->_requestedFrame.load() - app->_producedSequence) : 1;
        for (uint32_t frame = 0; frame < due && app->_playing; ++frame) {
            const int64_t coreStarted = esp_timer_get_time();
            if (inkConsoleFrame(app->_buttons.load(), app->_nesVideo ? nullptr : app->_workFrame)) {
                const uint32_t workUs = esp_timer_get_time() - coreStarted;
                app->_emulationUs.fetch_add(workUs);
                const uint32_t sequence = ++app->_producedSequence;
                // Keep image preparation off the scan-driving core.
                const int64_t packingStarted = esp_timer_get_time();
                app->packVideo(app->_packedFrame);
                const uint32_t packingUs = esp_timer_get_time() - packingStarted;
#if defined(BOARD_WAVESHARE_RLCD42)
                // Sound for this frame; waits only while the I2S queue is full.
                static int16_t pcm[1024];
                GameAudio& audio = GameAudio::getInstance();
                if (audio.running())
                    if (const size_t samples = inkConsoleAudio(pcm, 1024)) audio.write(pcm, samples);
#endif
                app->_packingUs.fetch_add(packingUs);
                // Publish only complete pictures at scan boundaries.
                xSemaphoreTake(app->_frameMutex, portMAX_DELAY);
                std::swap(app->_latest, app->_packedFrame);
                app->_latestSequence = sequence;
                app->_frameReady = true;
                xSemaphoreGive(app->_frameMutex);
                ++app->_drawnFrames;
                if (workUs + packingUs > app->_gamePeriodUs) app->_overBudgetFrames.fetch_add(1);
                ++app->_frames;
            } else { app->_failed = true; app->_playing = false; }
        }
        xSemaphoreGive(app->_mutex);
        if (synchronized) {
            // A slow core always has a queued scan notification: taking it
            // does NOT block, so IDLE0 would otherwise starve and trip TWDT.
            // Actually block for a tick, outside both locks. taskYIELD alone
            // cannot run the lower-priority idle task. Keep the watchdog on.
            vTaskDelay(1);
            continue;
        }
        next += app->_gamePeriodUs;
        int64_t remaining = next - esp_timer_get_time();
        if (remaining < -100000) next = esp_timer_get_time();
        vTaskDelay(std::max(TickType_t(1),pdMS_TO_TICKS(remaining > 1000 ? remaining / 1000 : 1)));
    }
}

void AppPaperboy::update() {
    if (_failed.exchange(false)) {
        _coreFault = true;
        _message = inkConsoleError();
        Serial.printf("Inkade stopped: %s\n", _message.c_str());
        pauseGame();
    }
    // A Bluetooth gamepad drives the game directly while playing (through
    // the Controls mapping), and navigates menus (key actions) otherwise.
    // The Controls screen also reads it raw while capturing a button.
    GamepadMgr& pad = GamepadMgr::getInstance();
    if (_controls) updateControls();
    pad.setRawMode(_playing || (_controls && (_capture >= 0 || _captureRelease)));
    const uint16_t padButtons = _playing ? pad.buttons() : 0;
    const bool padHome = PadMapping::pauses(_padMap, padButtons);
    if (padHome && !_padHomeHeld) { _padHomeHeld = true; back(); return; }  // Home pauses
    _padHomeHeld = padHome;
    if (!_playing) return;
    InputMgr::TouchPoint points[5];
    uint8_t count = InputMgr::getInstance().heldTouches(points);
    uint16_t buttons=0;
    for (uint8_t i = 0; i < count; ++i) {
        int x = points[i].x, y = points[i].y;
        buttons |= InkBoyUi::buttonsAt(x,y,inkConsoleSystem());
    }
#if defined(BOARD_SEEED_STICKY)
    // The side keys double as A and B while held, alongside the touch pad.
    if (digitalRead(PIN_BUTTON_UP) == LOW) buttons |= 1;
    if (digitalRead(PIN_BUTTON_DOWN) == LOW) buttons |= 2;
#endif
    // Pad: the Controls mapping, in the Game Boy / NES button bit order.
    buttons |= PadMapping::gameButtons(_padMap, padButtons);
    _buttons = buttons;
    if (buttons) BatteryMgr::getInstance().resetIdleTimer();
    if (millis() - _lastStats >= 5000) {
        float seconds = (millis() - _lastStats) / 1000.f;
        const unsigned frames = _frames.exchange(0);
        const unsigned drawn = _drawnFrames.exchange(0);
        const uint32_t work = _emulationUs.exchange(0), packing = _packingUs.exchange(0), slow = _overBudgetFrames.exchange(0);
        Serial.printf("Inkade %dx %s: emulator %.1f fps; rendered %.1f fps; core %.2f + pack %.2f ms (%u slow); panel %.1f scans/s; handoff %.2f ms; drive %.2f ms\n",
                      _nesVideo ? NesViewport::SCALE : GameViewport::SCALE, modeLabel(), frames/seconds, drawn/seconds,
                      frames ? work/(1000.f*frames) : 0, frames ? packing/(1000.f*frames) : 0, slow, _updates/seconds,
                      _regularUpdates ? _renderUs / (1000.f * _regularUpdates) : 0,
                      _regularUpdates ? _refreshUs / (1000.f * _regularUpdates) : 0);
        Serial.printf("Inkade frame sync: %s; fresh %u; repeated %u; skipped %u; maintenance %u; frame %u\n",
                      _videoEnabled ? "scan-boundary" : "native-clock fallback", _presentations.fresh,
                      _presentations.repeated, _presentations.skipped, _presentations.maintenance, _snapshotSequence);
        Serial.printf("Inkade presentation: %.1f distinct frames/s\n",_presentations.fresh/seconds);
        _presentations.clearCounts();
        _renderUs = _refreshUs = _regularUpdates = 0;
        _updates = 0; _lastStats = millis();
    }
}

void AppPaperboy::paintGame() {
#if defined(BOARD_WAVESHARE_RLCD42)
    paintRlcdGame();
    return;
#endif
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setFullWindow(); d.fillScreen(GxEPD_WHITE);
    label(d, "< Pause / saves", 20, 24);
    label(d, inkConsoleName(), PAUSE.systemLabelX, 24, 1);
    InkBoyUi::draw(d,inkConsoleSystem());
}

void AppPaperboy::paintPause() {
#if defined(BOARD_WAVESHARE_RLCD42)
    paintRlcdPause();
    return;
#endif
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false); d.setFullWindow(); d.fillScreen(GxEPD_WHITE);
    label(d, "< Resume game", 20, 24);
    label(d, "Game saves", 20, 85, 3);
    auto button = [&](const String& text, int x, int y, int w, int h) {
        d.drawRoundRect(x,y,w,h,8,GxEPD_BLACK); label(d,text,x+14,y+(h-16)/2);
    };
    const PauseLayout& p = PAUSE;
    if (_confirmation) {
        label(d, _confirmation == 1 ? "Replace this save slot?" :
                 _confirmation == 2 ? "Load this save state?" : "Restart from the title screen?", p.left, p.confirmTextY);
        label(d, _confirmation == 2 ? "Unsaved play will be discarded." :
                 _confirmation == 3 ? "Existing slots are kept." : "The old slot will be replaced.", p.left, p.confirmNoteY);
        button("Cancel",p.left,p.confirmY,p.confirmW,p.confirmH);
        button("Confirm",p.confirmRightX,p.confirmY,p.confirmW,p.confirmH);
    } else {
        button("Resume game",p.left,p.resumeY,p.width,p.resumeH);
        for (unsigned i = 0; i < 4; ++i) {
            int y = p.slotY + i*p.slotStep;
            label(d, i ? "Slot " + String(i) : "Auto resume",p.left,y+10);
            label(d,_slots[i] ? "Saved" : "Empty",p.left,y+37);
            if (i) button("Save",p.saveX,y,p.slotButtonW,p.slotH);
            if (_slots[i]) button("Load",p.loadX,y,p.slotButtonW,p.slotH);
        }
        button("Save & exit to ROM list",p.left,p.exitY,p.width,p.exitH);
        button("Restart game",p.left,p.restartY,p.width,p.restartH);
        label(d,_message,p.left,p.messageY);
        label(d,"Auto resume saves on exit / sleep.",p.left,p.noteY);
    }
    d.refresh(false);
}

void AppPaperboy::draw() {
    auto& d = DisplayMgr::getInstance().getDisplay();
#if INKADE_INKBOY_VIDEO
    if (_screen == SCREEN_TEST) { drawTest(); return; }
    if (_playing) { drawStickyGame(); return; }
#endif
    if (_playing) {
        bool repaint = _redraw || _forceClean;
        if (_redraw) { paintGame(); d.refresh(false); _redraw = false; _forceClean = true; }
        bool clean = _forceClean; // No timer: tap the game window to clean it.
        // Wait BEFORE latching the target, so a frame that finishes near the
        // deadline can still be used. The old snapshot's DMA is already drained.
        if (_videoEnabled) waitForScanSlot(clean);
        uint32_t started = micros();
        if (_frameReady.load()) {
            xSemaphoreTake(_frameMutex, portMAX_DELAY);
            if (_frameReady.exchange(false)) {
                // Triple buffering leaves the displayed snapshot immutable
                // while the emulator writes its separate work buffer.
                repaint = repaint || _videoEnabled || memcmp(_snapshot, _latest, packedBytes()) != 0;
                std::swap(_snapshot, _latest);
                _snapshotSequence = _latestSequence;
                _snapshotValid = true;
            }
            xSemaphoreGive(_frameMutex);
        }
        // Bind even an identical replacement: the previous snapshot becomes
        // writable by the producer after a swap. This snapshot stays immutable
        // until the NEXT draw, after all queued display reads have drained.
        d.setGameFrame(_snapshot);
        if (!_videoEnabled && !repaint && !clean) return;
        uint32_t rendered = micros();
        const uint32_t shownBefore = _snapshotSequence;
        scanStarted(clean);
        // Software VSYNC: latch this complete target, then let the other core
        // prepare NEXT scan's frame while this scan owns its immutable snapshot.
        // Maintenance is explicitly paused/rebased after the blocking refresh.
        if (!clean) requestNextFrame(false);
        if (!d.refreshGame(clean, _videoEnabled)) {
            // A missing video-state allocation must not strand the user.
            _videoEnabled = false; _forceClean = true;
            resetFrameTiming();
            Serial.println("Inkade: video unavailable, reverting to four-pass mode");
            return;
        }
        _presentations.observe(_snapshotSequence, _snapshotValid, clean);
        countFps(_snapshotSequence != shownBefore);
        if (clean) {
            // The final reset scan just drained; give the primed next frame a
            // full period before latching again instead of immediately repeating.
            _lastScanStarted = micros();
            requestNextFrame(true);
        }
        ++_updates;
        if (!clean) {
            _renderUs += rendered - started;
            _refreshUs += micros() - _lastScanStarted;
            ++_regularUpdates;
        }
        _forceClean = false;
        if (clean) _lastClean=millis();
        return;
    }
    if (!_redraw) return;
    _redraw = false;
    if (_controls) { paintControls(); return; }
    if (_paused) { paintPause(); return; }
#if INKADE_INKBOY_VIDEO
    if (_screen == SCREEN_SETTINGS) { paintSettings(); return; }
#endif
#if defined(BOARD_WAVESHARE_RLCD42)
    paintRlcdLibrary();
    return;
#endif
    d.setGameMode(false);
    d.setFullWindow();
    InkBoyLibraryUi::header(d,_systemMask,_selectedSystem,_filtered.size());
    for (unsigned row=0; row<InkBoyLibraryUi::ROWS; ++row) {
        unsigned index=_page*InkBoyLibraryUi::ROWS+row;
        if(index>=_filtered.size())break;
        const String& path=_roms[_filtered[index]];
        String name=path.substring(6),ext=name.substring(name.lastIndexOf('.')+1);ext.toUpperCase();
        name=name.substring(0,name.lastIndexOf('.'));name.replace('_',' ');
        String statePath=path+".inkdeck.sav";
        bool resume=EbookFS.exists(statePath+".state0")||EbookFS.exists(statePath+".state0.bak");
        String detail=ext+(resume?"  /  RESUME SAVED GAME":"  /  TAP TO PLAY");
        InkBoyLibraryUi::card(d,row,name.c_str(),detail.c_str());
        if (_cursorShown && index == _cursor) {
            // Key/gamepad selection: a heavier frame around the card.
            const int y = InkBoyLibraryUi::ROW_Y + row * InkBoyLibraryUi::ROW_STEP;
            for (int inset = 1; inset <= 3; ++inset)
                d.drawRoundRect(InkBoyLibraryUi::LEFT + inset, y + inset, InkBoyLibraryUi::CONTENT_W - 2 * inset,
                                InkBoyLibraryUi::ROW_H - 2 * inset, 9, GxEPD_BLACK);
        }
    }
    if(_filtered.empty()) {
        label(d,"Your next adventure starts here.",40,365,2);
        label(d,"Copy ROMs to /roms on the MicroSD card",40,420,1);
        label(d,"or upload them from InkDeck's web interface.",40,443,1);
        label(d,"Game Boy  .gb / .gbc",40,512,2);
#if !INKADE_INKBOY_VIDEO
        label(d,"NES       .nes",40,552,2);
#endif
    }
    InkBoyLibraryUi::footer(d,_page,(_filtered.size()+InkBoyLibraryUi::ROWS-1)/InkBoyLibraryUi::ROWS,_message.c_str());
    d.refresh(false);
}
#endif
