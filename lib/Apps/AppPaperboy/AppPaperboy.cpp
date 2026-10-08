#include "AppPaperboy.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
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
#include "InkBoyUi.h"
#include "InkBoyLibraryUi.h"
#include <esp_timer.h>
#include <WiFi.h>
#include <algorithm>
#include <soc/soc_memory_types.h>
#include "NetworkWork.h"

namespace {
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
    // freeing them later cannot recover a contiguous 64 KB for Genesis RAM.
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
        display.fillScreen(GxEPD_WHITE);
        label(display,"Preparing Ink Boy",30,180,3);
        label(display,"Finishing network activity...",30,250,2);
        display.refresh(false);
        Serial.println("Ink Boy: waiting for network owners before radio shutdown");
        while (!NetworkWork::gate().idle()) vTaskDelay(pdMS_TO_TICKS(10));
    }
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
    _message = "Add GB, NES or Genesis ROMs to /roms on SD.";
    if (ebookStorageUsesSD()) {
        File dir = EbookFS.open("/roms");
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                String name = f.name(); name = name.substring(name.lastIndexOf('/') + 1);
                InkSystem system=inkRomSystem(name.c_str());
                if (!f.isDirectory() && inkRomFilename(name.c_str()) && inkRomSizeValid(system,f.size()) && _roms.size() < 256) {
                    _roms.push_back("/roms/" + name); _systemMask|=1<<system;
                }
                f.close();
            }
        }
        if (dir) dir.close();
        std::sort(_roms.begin(), _roms.end(), [](const String& a,const String& b){ return strcasecmp(a.c_str(),b.c_str())<0; });
        if (!_roms.empty()) _message = "Tap a game to play or resume. Saves stay with each ROM.";
    }
    if(!(_systemMask&(1<<_selectedSystem))) {
        _selectedSystem=INK_SYSTEM_NONE;
        for(int i=1;i<=3;i++)if(_systemMask&(1<<i)){_selectedSystem=(InkSystem)i;break;}
    }
    filterRoms();
    _redraw = true;
}
void AppPaperboy::filterRoms() {
    _filtered.clear();_page=0;
    for(unsigned i=0;i<_roms.size();i++)if(inkRomSystem(_roms[i].c_str())==_selectedSystem)_filtered.push_back(i);
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
    else GameViewport::pack3x(destination,_workFrame,_gameRotation);
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
    Serial.printf("Ink Boy state %u: %s\n", slot, _message.c_str());
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
    Serial.printf("Ink Boy load %u: %s\n", slot, _message.c_str());
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
    if (_task) xTaskNotifyGive(_task);
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    xSemaphoreTake(_mutex, portMAX_DELAY); // Wait for a complete core frame.
    xSemaphoreGive(_mutex);
    _paused = true; _confirmation = 0; _redraw = true;
    inspectSlots();
}
void AppPaperboy::resumeGame() {
    if (_coreFault) { _message = "Load a save or restart the game"; _redraw = true; return; }
    auto& d = DisplayMgr::getInstance().getDisplay();
    _paused = false; _confirmation = 0;
    _redraw = false; _forceClean = true; _lastScanStarted = 0;
    paintGame(); d.refresh(false); d.setGameMode(true);
    _lastStats = millis(); _frames = 0; _drawnFrames = 0; _updates = 0;
    _emulationUs = _packingUs = _overBudgetFrames = 0;
    _renderUs = _refreshUs = _regularUpdates = 0;
    _presentations.clearCounts();
    resetFrameTiming(); _playing = true;
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
    if (_task) xTaskNotifyGive(_task); // Never leave a producer waiting on exit.
    // Neutralize pending video drive before SD saves or other blocking work.
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    if (!_savePath.isEmpty() && !_coreFault && (!saveState(0) || !save()))
        Serial.println("Ink Boy: save failed; previous saves preserved");
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
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
    _storageOwned = false;
    RomStorage::release(RomStorage::Game);
    NetworkWork::gate().resume();
}

void AppPaperboy::back() {
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
    d.fillScreen(GxEPD_WHITE); label(d,"Ink Boy",30,100,4);
    label(d,"Opening your game",30,205,3);
    label(d,String(inkSystemName(selected))+" emulator",30,264,2);
    String title=path.substring(path.lastIndexOf('/')+1);if(title.length()>38)title=title.substring(0,35)+"...";
    label(d,title,30,310,2);label(d,"Loading ROM and checking saved progress...",30,386,1);d.refresh(false);
    File f = EbookFS.open(path, "r");
    const size_t bytes = f ? f.size() : 0;
    if (!inkRomSizeValid(selected,bytes)) { _message = "Invalid ROM size for this system"; _redraw = true; return; }
    xSemaphoreTake(_mutex, portMAX_DELAY);
    inkConsoleClose(); free(_rom); _rom = nullptr;
    // No renderer may retain a borrowed frame while changing its allocation.
    // Keep the larger Genesis/NES allocations in PSRAM, leaving internal RAM
    // available for display DMA. GB retains its internal-first preference.
    d.setGameMode(false);
    if(!configureVideo(selected==INK_SYSTEM_NES)) {
        if (_romFile) _romFile.close();
        f.close();_savePath="";_message="Not enough NES video memory";_redraw=true;
        xSemaphoreGive(_mutex);return;
    }
    d.setNesGame(_nesVideo);
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
    _rom = (uint8_t*)ps_malloc(loaded);
    bool readOk = _rom && f.read(_rom, loaded) == loaded;
    bool ok = readOk;
    if (ok && banked) {
        _romFile = f;
        ok = inkConsoleOpen(selected,_rom, bytes, [](void* context, size_t offset, uint8_t* out, size_t count) {
            File* file = static_cast<File*>(context);
            return file->seek(offset) && file->read(out, count) == count;
        }, &_romFile);
    } else {
        f.close();
        if (ok) ok = inkConsoleOpen(selected,_rom, bytes,nullptr,nullptr);
    }
    if (ok) {
        _savePath = path + (selected == INK_SYSTEM_SEGA ? ".inkdeck.clown.sav" : ".inkdeck.sav");
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
    } else _message = !_rom ? "Not enough ROM memory" : !readOk ? "Could not read ROM from SD" : inkConsoleError();
    _producedSequence = _latestSequence = _snapshotSequence = 0;
    _snapshotValid = false;
    xSemaphoreGive(_mutex);
    _failed = false; _frameReady = false;
    if (ok) {
        _gameRotation = d.getRotation(); // Stable until gameplay stops.
        _gamePeriodUs=inkConsolePeriod();
        memset(_snapshot, _nesVideo ? 0 : 0xff, packedBytes()); // NES shade 0 / GB bit 1 are white.
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
        Serial.println(_nesVideo ? "Ink Boy NES 2x: native 256x240, window 512x480, four shades, fixed Contrast" :
                                  "Ink Boy 3x: fixed Contrast, 6 pulses, 540 rows, 20MHz, window 480x432");
        Serial.printf("Game core: %s; %u us/frame (grayscale, no audio); CPU %u MHz\n",inkConsoleName(),_gamePeriodUs,getCpuFrequencyMhz());
        _lastClean = _lastStats = millis(); _frames = 0; _drawnFrames = 0; _updates = 0;
        if (autoFailed) { _paused = true; _redraw = true; }
        else resumeGame();
    } else {
        Serial.printf("Ink Boy load failed: %s\n", _message.c_str());
        xSemaphoreTake(_mutex, portMAX_DELAY);
        inkConsoleClose(); free(_rom); _rom = nullptr;
        if (_romFile) _romFile.close();
        xSemaphoreGive(_mutex);
        _savePath = ""; _redraw = true;
    }
}

void AppPaperboy::tap(uint16_t x, uint16_t y) {
    if (y < (_playing && _nesVideo ? NesViewport::Y : 60)) { back(); return; }
    if (_paused) {
        if (_confirmation) {
            if (inside(x,y,20,460,225,80)) { _confirmation = 0; _redraw = true; }
            else if (inside(x,y,275,460,225,80)) {
                uint8_t action = _confirmation; _confirmation = 0;
                if (action == 1) saveState(_selectedSlot);
                else if (action == 2) { if (loadState(_selectedSlot)) { resumeGame(); return; } }
                else {
                    xSemaphoreTake(_mutex, portMAX_DELAY);
                    bool ok = inkConsoleRestart();
                    _frameReady = false; _snapshotValid = false;
                    memset(_snapshot, _nesVideo ? 0 : 0xff, packedBytes());
                    xSemaphoreGive(_mutex);
                    _coreFault = !ok;
                    if (ok) { resumeGame(); return; }
                    _message = inkConsoleError();
                }
                _redraw = true;
            }
        } else if (inside(x,y,20,125,480,70)) resumeGame();
        else if (y >= 240 && y < 600) {
            unsigned slot = (y - 240) / 90;
            if ((y-240) % 90 < 70) {
                _selectedSlot = slot;
                if (x >= 370 && x < 500 && _slots[slot]) _confirmation = 2;
                else if (x >= 225 && x < 355 && slot && !_coreFault) {
                    if (_slots[slot]) _confirmation = 1; else saveState(slot);
                }
                _redraw = true;
            }
        } else if (inside(x,y,20,650,480,75)) { exitGame(); }
        else if (inside(x,y,20,750,480,70)) { _confirmation = 3; _redraw = true; }
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
    } else if (y >= InkBoyLibraryUi::NAV_Y && y < InkBoyLibraryUi::NAV_Y+InkBoyLibraryUi::NAV_H) {
        if (x>=20&&x<164&&_page) --_page;
        else if (x>=360&&x<504&&(_page+1)*InkBoyLibraryUi::ROWS<_filtered.size()) ++_page;
        else if (x>=190&&x<334) scan();
        _redraw = true;
    }
}

void AppPaperboy::resetFrameTiming() {
    // Opening a game or entering the safety fallback waits only for current
    // emulation, never for a VSYNC while holding this mutex.
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _scanSynchronized = _videoEnabled;
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
            const bool render = GameFrameTiming::renderCatchUpFrame(
                inkConsoleSystem()==INK_SYSTEM_SEGA,frame,due);
            if (inkConsoleStep(app->_buttons.load(), app->_nesVideo ? nullptr : app->_workFrame,render)) {
                const uint32_t workUs = esp_timer_get_time() - coreStarted;
                app->_emulationUs.fetch_add(workUs);
                const uint32_t sequence = ++app->_producedSequence;
                uint32_t packingUs = 0;
                if (render) {
                    // Keep image preparation off the scan-driving core.
                    const int64_t packingStarted = esp_timer_get_time();
                    app->packVideo(app->_packedFrame);
                    packingUs = esp_timer_get_time() - packingStarted;
                    app->_packingUs.fetch_add(packingUs);
                    // Publish only complete pictures; skipped drawing still
                    // advances the simulation sequence and native frame clock.
                    xSemaphoreTake(app->_frameMutex, portMAX_DELAY);
                    std::swap(app->_latest, app->_packedFrame);
                    app->_latestSequence = sequence;
                    app->_frameReady = true;
                    xSemaphoreGive(app->_frameMutex);
                    ++app->_drawnFrames;
                }
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
        Serial.printf("Ink Boy stopped: %s\n", _message.c_str());
        pauseGame();
    }
    if (!_playing) return;
    InputMgr::TouchPoint points[5];
    uint8_t count = InputMgr::getInstance().heldTouches(points);
    uint16_t buttons=0;
    for (uint8_t i = 0; i < count; ++i) {
        int x = points[i].x, y = points[i].y;
        buttons |= InkBoyUi::buttonsAt(x,y,inkConsoleSystem());
    }
    _buttons = buttons;
    if (buttons) BatteryMgr::getInstance().resetIdleTimer();
    if (millis() - _lastStats >= 5000) {
        float seconds = (millis() - _lastStats) / 1000.f;
        const unsigned frames = _frames.exchange(0);
        const unsigned drawn = _drawnFrames.exchange(0);
        const uint32_t work = _emulationUs.exchange(0), packing = _packingUs.exchange(0), slow = _overBudgetFrames.exchange(0);
        Serial.printf("Ink Boy %dx %s: emulator %.1f fps; rendered %.1f fps; core %.2f + pack %.2f ms (%u slow); panel %.1f scans/s; handoff %.2f ms; drive %.2f ms\n",
                      _nesVideo ? 2 : 3, modeLabel(), frames/seconds, drawn/seconds,
                      frames ? work/(1000.f*frames) : 0, frames ? packing/(1000.f*frames) : 0, slow, _updates/seconds,
                      _regularUpdates ? _renderUs / (1000.f * _regularUpdates) : 0,
                      _regularUpdates ? _refreshUs / (1000.f * _regularUpdates) : 0);
        Serial.printf("Ink Boy frame sync: %s; fresh %u; repeated %u; skipped %u; maintenance %u; frame %u\n",
                      _videoEnabled ? "scan-boundary" : "native-clock fallback", _presentations.fresh,
                      _presentations.repeated, _presentations.skipped, _presentations.maintenance, _snapshotSequence);
        Serial.printf("Ink Boy presentation: %.1f distinct frames/s\n",_presentations.fresh/seconds);
        _presentations.clearCounts();
        _renderUs = _refreshUs = _regularUpdates = 0;
        _updates = 0; _lastStats = millis();
    }
}

void AppPaperboy::paintGame() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.fillScreen(GxEPD_WHITE);
    label(d, "< Pause / saves", 20, 24);
    label(d, inkConsoleName(), 350, 24, 1);
    InkBoyUi::draw(d,inkConsoleSystem());
}

void AppPaperboy::paintPause() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false); d.fillScreen(GxEPD_WHITE);
    label(d, "< Resume game", 20, 24);
    label(d, "Game saves", 20, 85, 3);
    auto button = [&](const String& text, int x, int y, int w, int h) {
        d.drawRoundRect(x,y,w,h,8,GxEPD_BLACK); label(d,text,x+14,y+(h-16)/2);
    };
    if (_confirmation) {
        label(d, _confirmation == 1 ? "Replace this save slot?" :
                 _confirmation == 2 ? "Load this save state?" : "Restart from the title screen?", 20, 280);
        label(d, _confirmation == 2 ? "Unsaved play will be discarded." :
                 _confirmation == 3 ? "Existing slots are kept." : "The old slot will be replaced.", 20, 330);
        button("Cancel",20,460,225,80); button("Confirm",275,460,225,80);
    } else {
        button("Resume game",20,125,480,70);
        for (unsigned i = 0; i < 4; ++i) {
            int y = 240 + i*90;
            label(d, i ? "Slot " + String(i) : "Auto resume",20,y+10);
            label(d,_slots[i] ? "Saved" : "Empty",20,y+37);
            if (i) button("Save",225,y,130,70);
            if (_slots[i]) button("Load",370,y,130,70);
        }
        button("Save & exit to ROM list",20,650,480,75);
        button("Restart game",20,750,480,70);
        label(d,_message,20,850);
        label(d,"Auto resume saves on exit / sleep.",20,900);
    }
    d.refresh(false);
}

void AppPaperboy::draw() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    if (_playing) {
        bool repaint = _redraw || _forceClean;
        if (_redraw) { paintGame(); d.refresh(false); _redraw = false; _forceClean = true; }
        bool clean = _forceClean; // No timer: tap the game window to clean it.
        // Wait BEFORE latching the target, so a frame that finishes near the
        // deadline can still be used. The old snapshot's DMA is already drained.
        if (_videoEnabled && !clean && _lastScanStarted) {
            const uint32_t elapsed = micros() - _lastScanStarted;
            if (elapsed < 16667) delayMicroseconds(16667 - elapsed);
        }
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
        _lastScanStarted = micros();
        // Software VSYNC: latch this complete target, then let the other core
        // prepare NEXT scan's frame while this scan owns its immutable snapshot.
        // Maintenance is explicitly paused/rebased after the blocking refresh.
        if (!clean) requestNextFrame(false);
        if (!d.refreshGame(clean, _videoEnabled)) {
            // A missing video-state allocation must not strand the user.
            _videoEnabled = false; _forceClean = true;
            resetFrameTiming();
            Serial.println("Ink Boy: video unavailable, reverting to four-pass mode");
            return;
        }
        _presentations.observe(_snapshotSequence, _snapshotValid, clean);
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
    if (_paused) { paintPause(); return; }
    d.setGameMode(false);
    InkBoyLibraryUi::header(d,_systemMask,_selectedSystem,_filtered.size());
    for (unsigned row=0; row<InkBoyLibraryUi::ROWS; ++row) {
        unsigned index=_page*InkBoyLibraryUi::ROWS+row;
        if(index>=_filtered.size())break;
        const String& path=_roms[_filtered[index]];
        String name=path.substring(6),ext=name.substring(name.lastIndexOf('.')+1);ext.toUpperCase();
        name=name.substring(0,name.lastIndexOf('.'));name.replace('_',' ');
        String statePath=path+(inkRomSystem(path.c_str())==INK_SYSTEM_SEGA?".inkdeck.clown.sav":".inkdeck.sav");
        bool resume=EbookFS.exists(statePath+".state0")||EbookFS.exists(statePath+".state0.bak");
        String detail=ext+(resume?"  /  RESUME SAVED GAME":"  /  TAP TO PLAY");
        InkBoyLibraryUi::card(d,row,name.c_str(),detail.c_str());
    }
    if(_filtered.empty()) {
        label(d,"Your next adventure starts here.",40,365,2);
        label(d,"Copy ROMs to /roms on the MicroSD card",40,420,1);
        label(d,"or upload them from InkDeck's web interface.",40,443,1);
        label(d,"Game Boy  .gb / .gbc",40,512,2);
        label(d,"NES       .nes",40,552,2);
        label(d,"Genesis   .md / .gen / .bin",40,592,2);
    }
    InkBoyLibraryUi::footer(d,_page,(_filtered.size()+InkBoyLibraryUi::ROWS-1)/InkBoyLibraryUi::ROWS,_message.c_str());
    d.refresh(false);
}
#endif
