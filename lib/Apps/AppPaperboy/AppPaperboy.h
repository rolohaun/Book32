#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY) || defined(BOARD_WAVESHARE_RLCD42)
#include "BaseApp.h"
#include "InputMgr.h"
#include "GameFrameTiming.h"
#include "RomFormat.h"
#include "PadMapping.h"
#include <atomic>
#include <vector>
#include <FS.h>
#include <freertos/semphr.h>
#if INKADE_INKBOY_VIDEO
#include "InkadeSettings.h"
#endif

class AppPaperboy : public App {
public:
    const char* getName() override { return "Inkade"; }
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
    uint8_t whiteFill() const;
    bool _padHomeHeld = false;
    // Gamepad mapping and its Controls screen. See AppPaperboyControls.cpp.
    PadMapping::Map _padMap = PadMapping::DEFAULTS;
    // A Bluetooth keyboard has its own: any key for each action.
    KeyMapping::Map _keyMap = KeyMapping::DEFAULTS;
    bool _controls = false, _controlsFull = false;
    int _controlsCursor = -1;    // row chosen with keys / gamepad, -1 none
    int _capture = -1;           // action waiting for a gamepad press, -1 none
    bool _captureArmed = false;  // the pad was released since capture began
    bool _captureRelease = false;  // pad stays raw until the captured press ends
    String _controlsMessage;
    void loadPadMap();
    void savePadMap();
    // Controls show and capture keys while a keyboard is connected.
    bool keyboardControls() const;
    String controlValue(int action) const;  // "X", or "A, Start" for a pad
    void openControls();
    void paintControls();
    void controlsTap(uint16_t x, uint16_t y);
    void controlsKey(InputAction a);
    void controlsBack();
    void activateControlRow(int row);
    void updateControls();
    unsigned _cursor = 0;        // library row chosen with keys / gamepad
    bool _cursorShown = false;   // shown after the first key press
    void keyAction(InputAction a);
    // Scan pacing: a fixed schedule of 1/fps slots, or (cap 0) at most one
    // scan per 16.7 ms as fast as the panel goes.
    void waitForScanSlot(bool clean);
    void scanStarted(bool clean);
    // On-screen FPS counter (Sticky): distinct frames shown per second.
    std::atomic<uint8_t> _fpsShown{0};
    unsigned _fpsCount = 0;
    unsigned long _fpsAt = 0;
    void countFps(bool fresh);
#if defined(BOARD_WAVESHARE_RLCD42)
    // RLCD: no touch, so every screen is a list moved through with keys
    // (KEY: next, hold to select; BOOT: previous, hold for back) or a
    // gamepad. Games and their pause menu are landscape. See AppPaperboyRlcd.cpp.
    enum PauseItem : uint8_t { PAUSE_RESUME, PAUSE_SAVE, PAUSE_LOAD, PAUSE_RESTART, PAUSE_EXIT, PAUSE_SOUND,
                               PAUSE_CANCEL, PAUSE_CONFIRM };
    struct PauseRow { PauseItem item; uint8_t slot; };
    int _menuCursor = 0;  // pause menu row
    std::vector<PauseRow> pauseRows() const;
    uint8_t landscapeRotation() const { return _gameRotation == 1 ? 2 : 0; }
    void useLandscape(bool landscape);
    void rlcdKey(InputAction a);
    void rlcdLibraryKey(InputAction a);
    void rlcdPauseKey(InputAction a);
    void rlcdControlsKey(InputAction a);
    void paintRlcdLibrary();
    void paintRlcdPause();
    void paintRlcdGame();
    void paintRlcdControls();
#endif
#if INKADE_INKBOY_VIDEO
    // Display settings and transition test (Sticky). See AppPaperboyDisplay.cpp.
    enum Screen : uint8_t { SCREEN_LIBRARY, SCREEN_SETTINGS, SCREEN_TEST };
    enum SettingsPage : uint8_t { PAGE_MAIN, PAGE_DOSES, PAGE_HOLDS, PAGE_ROUTES, PAGE_PRESETS };
    Screen _screen = SCREEN_LIBRARY;
    SettingsPage _settingsPage = PAGE_MAIN;
    InkadeSettings::Active _display;
    std::vector<InkadeSettings::PresetFile> _presets;
    String _settingsMessage;
    bool _testPaused = false;
    int64_t _testStarted = 0, _testFrozen = 0;  // esp_timer microseconds
    std::vector<int> settingsRows() const;
    String rowLabel(int id) const;
    String rowValue(int id) const;
    bool rowIsAction(int id) const;
    void changeRow(int id, int delta);
    void settingsChanged(bool modeSetting);
    void openSettings();
    void paintSettings();
    void settingsTap(uint16_t x, uint16_t y);
    void startTest();
    void stopTest();
    void paintTest();
    void testTap(uint16_t x, uint16_t y);
    void drawTest();
    // InkBoy's display loop (game_session_play): 33.3 ms image clock, scans
    // only for a new image or remaining drive, analog off after 750 ms idle.
    void drawStickyGame();
    bool latestImage();
    int64_t _nextImage = 0, _lastChange = 0;
    bool _analogOn = false;
    void serialSetting(const char* line);
#endif
};
#endif
