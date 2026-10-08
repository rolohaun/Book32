#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include "BaseApp.h"
#include "GameFrameTiming.h"
#include "RomFormat.h"
#include <atomic>
#include <vector>
#include <FS.h>
#include <freertos/semphr.h>

class AppPaperboy : public App {
public:
    const char* getName() override { return "Ink Boy"; }
    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override { _redraw = true; }
private:
    std::vector<String> _roms;
    std::vector<unsigned> _filtered;
    InkSystem _selectedSystem = INK_SYSTEM_NONE;
    uint8_t _systemMask = 0;
    unsigned _page = 0;
    bool _redraw = true;
    bool _storageOwned = false;
    bool _forceClean = true;
    // Fixed six-pulse Contrast. Only a video-path failure can select
    // the internal four-pass fallback; there is no user-selectable video mode.
    bool _videoEnabled = true;
    uint32_t _renderUs = 0, _refreshUs = 0, _regularUpdates = 0;
    uint32_t _lastScanStarted = 0;
    GameFrameTiming::Clock _frameClock;
    GameFrameTiming::Presentations _presentations;
    uint32_t _producedSequence = 0; // Protected by the core mutex.
    uint32_t _latestSequence = 0;   // Protected with its pointer by frame mutex.
    uint32_t _snapshotSequence = 0;
    bool _snapshotValid = false;
    String _message, _savePath;
    bool _paused = false, _coreFault = false;
    bool _slots[4] = {false, false, false, false};
    uint8_t _confirmation = 0, _selectedSlot = 0;
    File _romFile;
    uint8_t *_rom = nullptr, *_latest = nullptr, *_snapshot = nullptr, *_workFrame = nullptr;
    uint8_t* _packedFrame = nullptr;
    uint8_t _gameRotation = 3;
    bool _nesVideo = false;
    SemaphoreHandle_t _mutex = nullptr;
    SemaphoreHandle_t _frameMutex = nullptr;
    TaskHandle_t _task = nullptr;
    std::atomic<bool> _playing{false}, _frameReady{false}, _failed{false};
    std::atomic<bool> _scanSynchronized{true};
    std::atomic<uint32_t> _requestedFrame{1};
    std::atomic<uint16_t> _buttons{0};
    uint32_t _gamePeriodUs = GameFrameTiming::PERIOD_US;
    std::atomic<unsigned> _frames{0}, _drawnFrames{0};
    std::atomic<uint32_t> _emulationUs{0}, _packingUs{0}, _overBudgetFrames{0};
    unsigned long _lastClean = 0, _lastStats = 0;
    unsigned _updates = 0;
    static void run(void* self);
    void scan();
    void filterRoms();
    void openBook(const String& path);
    bool save();
    bool saveState(unsigned slot);
    bool loadState(unsigned slot);
    void inspectSlots();
    void pauseGame();
    void resumeGame();
    void exitGame();
    void paintPause();
    void back();
    void tap(uint16_t x, uint16_t y);
    void paintGame();
    bool configureVideo(bool nes);
    void packVideo(uint8_t* destination);
    size_t packedBytes() const;
    void resetFrameTiming();
    void requestNextFrame(bool maintenance);
    const char* modeLabel() const { return _videoEnabled ? "Contrast" : "safe fallback"; }
};
#endif
