#include "LilygoDisplay.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Wire.h>
#include <driver/gpio.h>
#include <soc/soc_memory_types.h>
#include "GameViewport.h"
#include "NesViewport.h"
#include "GameVideoSelfTest.h"
#include "StickyGrayImage.h"
namespace { constexpr uint8_t FRONTLIGHT_CHANNEL = 7; }

void LilygoDisplay::init(uint32_t, bool, uint16_t, bool) {
#if LILYGO_T5S3_PLAIN
    // The queued video transport latches rows from the I80 DMA callback by
    // toggling LE as a GPIO. On this board LE is a bit of the 74HCT4094 control
    // register, so Inkade keeps to the four-pass partial updates.
    _videoAvailable = false;
#else
    _videoAvailable = gameVideoSelfTest();
    Serial.printf("Game video self-test (GB 3x, NES 2x, pulses, clipping, queued DMA, frame timing): %s\n",
                  _videoAvailable ? "PASS" : "FAIL");
#endif
    // Restore a known off level before releasing the deep-sleep pin hold.
    pinMode(PIN_FRONTLIGHT, OUTPUT);
    digitalWrite(PIN_FRONTLIGHT, LOW);
    gpio_hold_dis(gpio_num_t(PIN_FRONTLIGHT));
    gpio_deep_sleep_hold_dis();
    ledcSetup(FRONTLIGHT_CHANNEL, 500, 8); // PT4103 requires <= 1 kHz PWM.
    ledcAttachPin(PIN_FRONTLIGHT, FRONTLIGHT_CHANNEL);
    setFrontlight(false);
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    Wire.setTimeOut(20);
#if LILYGO_T5S3_PLAIN
    // Plain T5-4.7 S3: panel power and control lines through the 74HCT4094
    // shift register (the vendor ed047tc1.c protocol, as FastEPD's LilyGo*
    // callbacks implement it).
    // The predefined panel carries its size, and initPanel() allocates the
    // buffers; a second setPanelSize() would be refused (buffers exist).
    _ready = _panel.initPanel(BB_PANEL_LILYGO_T5S3, 10000000) == BBEP_SUCCESS;
    if (!_ready) Serial.println("LILYGO T5S3: panel init failed");
#else
    // Do not energize a panel using the older H752's incompatible pinout.
    Wire.beginTransmission(0x20);
    if (Wire.endTransmission() != 0) {
        Serial.println("LILYGO H752-01 expander missing; display disabled");
        return;
    }
    // Official H752-01 examples use the EPDiy V7 parallel/power mapping.
    _ready = _panel.initPanel(BB_PANEL_EPDIY_V7, 20000000) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setPanelSize(960, 540) == BBEP_SUCCESS;
#endif
    if (_ready) _ready = _panel.setMode(BB_MODE_1BPP) == BBEP_SUCCESS;
    setGrayPulses(StickyGrayImage::LILYGO_BLACKS, StickyGrayImage::LILYGO_WHITES);
    if (_ready) clearScreen();
}

void LilygoDisplay::copyCanvas(const uint8_t* canvas) {
    // Both buffers are MSB-first, native-landscape, white=1.
    memset(_panel.currentBuffer(), 0xff, 960 * 540 / 8);
    for (int y = 0; y < LilygoLayout::CANVAS_H; ++y)
        memcpy(_panel.currentBuffer() + (y + LilygoLayout::INSET) * 120 + LilygoLayout::INSET / 8,
               canvas + y * LilygoLayout::PITCH, LilygoLayout::PITCH);
}

void LilygoDisplay::refresh(bool partial) {
    if (!_ready || !getBuffer() || !_panel.currentBuffer()) return;
    stopVideo();
    if (!_ready) return;
    copyCanvas(getBuffer());
    if (_forceFull && partial) {
        // The reference is a 4-bit (grayscale) image. Instead of a flashing
        // full refresh, the inverse of the new frame becomes the reference,
        // so this partial drives every pixel and clears the grays.
        _panel.backupPlane();
        uint8_t* reference = _panel.previousBuffer();
        for (size_t i = 0; i < 960 * 540 / 8; ++i) reference[i] = ~reference[i];
    }
    _forceFull = false;
    int result = partial ? _panel.partialUpdate(_gaming)
                         : _panel.fullUpdate(CLEAR_SLOW, false);
    checkResult(result);
}

namespace {
// FastEPD keeps a pointer to the matrix: levels x passes, 1 darkens, 2 lightens.
constexpr int MAX_GRAY_PASSES = 32;
uint8_t grayMatrix[16 * MAX_GRAY_PASSES];
int grayPasses = 0;
}

void LilygoDisplay::setGrayPulses(const uint8_t* blacks, const uint8_t* whites) {
    int passes = 1;
    for (int level = 0; level < 16; ++level)
        passes = max(passes, blacks[level] + (whites ? whites[level] : 0));
    passes = min(passes, MAX_GRAY_PASSES);
    // Pulses come last, so every level finishes together.
    for (int level = 0; level < 16; ++level) {
        const int white = whites ? whites[level] : 0, start = passes - blacks[level] - white;
        for (int pass = 0; pass < passes; ++pass)
            grayMatrix[level * passes + pass] = pass < start ? 0 : pass < passes - white ? 1 : 2;
    }
    grayPasses = passes;
}

bool LilygoDisplay::drawGrayCover(uint8_t* gray, int x, int y, int w, int h) {
    using namespace LilygoLayout;
    if (!_ready || !getBuffer() || !_panel.currentBuffer() || _gaming || !gray) return false;
    int16_t* errors = static_cast<int16_t*>(ps_malloc(sizeof(int16_t) * 2 * (w + 2)));
    if (!errors) return false;
    StickyGrayImage::quantize(gray, w, h, errors, StickyGrayImage::LILYGO_TONES, StickyGrayImage::LILYGO_TONE_COUNT);
    free(errors);
    stopVideo();
    if (_panel.setMode(BB_MODE_4BPP) != BBEP_SUCCESS) return false;
    if (grayPasses && _panel.setCustomMatrix(grayMatrix, 16 * grayPasses) != BBEP_SUCCESS) {
        _panel.setMode(BB_MODE_1BPP);  // the caller falls back to a dithered cover
        return false;
    }
    // 4-bit native-landscape buffer, two pixels per byte (high nibble first),
    // 15 white: the canvas in black and white, then the cover in gray.
    uint8_t* panel = _panel.currentBuffer();
    constexpr int NATIVE_PITCH = PANEL_W / 2;
    memset(panel, 0xff, NATIVE_PITCH * PANEL_H);
    auto put = [&](int nx, int ny, uint8_t level) {
        uint8_t& pair = panel[ny * NATIVE_PITCH + nx / 2];
        pair = (nx & 1) ? (pair & 0xf0) | level : (pair & 0x0f) | (level << 4);
    };
    const uint8_t* canvas = getBuffer();
    for (int ry = 0; ry < CANVAS_H; ++ry)
        for (int rx = 0; rx < CANVAS_W; ++rx)
            if (!(canvas[ry * PITCH + rx / 8] & (0x80 >> (rx & 7)))) put(rx + INSET, ry + INSET, 0);
    // Bounds are the rotated (portrait) size: GFX's own WIDTH and HEIGHT,
    // which shadow LilygoLayout's here, are the raw landscape canvas.
    const uint8_t rotation = getRotation();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= height()) continue;
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= width()) continue;
            // GFXcanvas1's rotation (1 or 3) to the raw landscape canvas.
            const int rx = rotation == 1 ? CANVAS_W - 1 - py : py;
            const int ry = rotation == 1 ? px : CANVAS_H - 1 - px;
            put(rx + INSET, ry + INSET, gray[row * w + col]);
        }
    }
    const uint32_t started = millis();
    checkResult(_panel.fullUpdate(CLEAR_SLOW, false));
    _panel.setMode(BB_MODE_1BPP);
    // The previous-image buffer shares memory with the 4-bit frame.
    _forceFull = true;
    Serial.printf("LilygoDisplay: grayscale image %dx%d in %lu ms\n", w, h, millis() - started);
    return _ready;
}

namespace { constexpr uint8_t LIVE_PASSES = 3; }  // Normal partial updates use 4

bool LilygoDisplay::liveBegin() {
    if (_live) return true;
    if (!_ready || _gaming || !_panel.currentBuffer()) return false;
    stopVideo();
    _panel.setPasses(LIVE_PASSES);
    _live = _liveBase = true;
    return true;
}

bool LilygoDisplay::liveFrame(const uint8_t* frame) {
    if (!_live || !_ready || !frame) return false;
    copyCanvas(frame);
    if (_liveBase) {
        // The screen may hold a grayscale page, which a black-and-white
        // reference can't describe: with the inverse as the reference, the
        // first frame drives every pixel (no flash), clearing the grays.
        _panel.backupPlane();
        uint8_t* reference = _panel.previousBuffer();
        for (size_t i = 0; i < 960 * 540 / 8; ++i) reference[i] = ~reference[i];
        _liveBase = false;
    }
    checkResult(_panel.partialUpdate(true));
    return _ready;
}

void LilygoDisplay::liveEnd() {
    if (!_live) return;
    _panel.setPasses(4);
    _live = false;
    _forceFull = false;  // The reference is the last live frame (1-bit)
}

void LilygoDisplay::checkResult(int result) {
    if (result != BBEP_SUCCESS) {
        Serial.printf("Parallel display error %d; disabling panel\n", result);
        _panel.einkPower(0);
        _ready = false;
    }
}

void LilygoDisplay::clearScreen(uint8_t value) {
    if (getBuffer()) memset(getBuffer(), value, LilygoLayout::PITCH * LilygoLayout::CANVAS_H);
    refresh(false);
}

void LilygoDisplay::setGameMode(bool active) {
    stopVideo();
    if (!active) {
        free(_videoHistory); _videoHistory = nullptr;
        for(auto& bank:_nesHistoryBanks){free(bank);bank=nullptr;}
        _gameImage = nullptr; // Borrowed from AppPaperboy's compact triple buffer.
    } else {
        // Permanent profile: Contrast's six pulses, without the experimental
        // settled boost or scan tail. App entry performs a clean/history reset.
        _panel.setVideoPulses(6, false);
        _panel.setVideoScanTail(false);
    }
    _gaming = active;
    // Start with normal-strength drive. One pass can leave changed pixels gray
    // even though the differential driver has marked them as fully updated.
    _panel.setPasses(4); // Normal UI and internal low-memory fallback.
    if (!active && _ready) _panel.einkPower(0);
}

void LilygoDisplay::hibernate() {
    stopVideo();
    if (_ready) _panel.einkPower(0);
    setFrontlight(false);
    ledcDetachPin(PIN_FRONTLIGHT);
    pinMode(PIN_FRONTLIGHT, OUTPUT);
    digitalWrite(PIN_FRONTLIGHT, LOW);
    gpio_hold_en(gpio_num_t(PIN_FRONTLIGHT));
    gpio_deep_sleep_hold_en();
}

void LilygoDisplay::setFrontlight(bool on) {
    // PWM is independent of panel refresh and does not disturb game video.
    _frontlightOn = on;
    ledcWrite(FRONTLIGHT_CHANNEL, on ? (_brightness * 255 + 50) / 100 : 0);
}

void LilygoDisplay::setBrightness(uint8_t percent) {
    _brightness = constrain(percent, 10, 100);
    setFrontlight(_frontlightOn);
}

void LilygoDisplay::stopVideo() {
    if (_videoRunning && _ready) checkResult(_panel.videoNeutral());
    _videoRunning = false;
    _videoReset = true;
}

void LilygoDisplay::copyRegion(const BB_RECT& rect) {
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        memcpy(_panel.currentBuffer() + y * (960 / 8) + rect.x / 8,
               getBuffer() + (y - LilygoLayout::INSET) * LilygoLayout::PITCH +
               (rect.x - LilygoLayout::INSET) / 8, rect.w / 8);
}

bool LilygoDisplay::refreshGame(bool clean, bool smooth) {
    if (!_ready || !_gaming || !_gameImage || !getBuffer() || !_panel.currentBuffer()) return false;
    if (smooth && !_videoAvailable) return false;
    const uint8_t rotation = getRotation();
    if (rotation != 1 && rotation != 3) return false;
    const int rowRepeat = _nesGame ? 2 : 3;
    BB_RECT rect = {(_nesGame ? NesViewport::nativeX(rotation) : GameViewport::nativeX(rotation)) + LilygoLayout::INSET,
                    (_nesGame ? NesViewport::nativeY(rotation) : GameViewport::nativeY(rotation)) + LilygoLayout::INSET,
                    _nesGame ? NesViewport::H : GameViewport::H, _nesGame ? NesViewport::W : GameViewport::W};
    if (smooth && !_videoHistory && !_nesHistoryBanks[0]) {
        const size_t stateBytes = _nesGame ? NesViewport::HISTORY_BYTES : (rect.w / 2) * (rect.h / rowRepeat);
        // An all-internal requirement wastes usable SRAM if a single bank
        // cannot fit. Each smaller bank can fall back independently, while
        // reserving 20 KiB for runtime work and rolling back on real failure.
        if(_nesGame) {
            NesHistoryMemory::allocate(_nesHistoryBanks,
                [](){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);},
                [](size_t bytes){return (uint8_t*)heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);},
                [](size_t bytes){return (uint8_t*)ps_malloc(bytes);},
                [](uint8_t* bank){free(bank);});
        }
        if (!_nesGame)
            _videoHistory = (uint8_t*)heap_caps_malloc(stateBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!_videoHistory && !_nesHistoryBanks[0]) _videoHistory = (uint8_t*)ps_malloc(stateBytes);
        if (!_videoHistory && !_nesHistoryBanks[0]) return false; // App falls back to the known four-pass mode.
        if (_nesGame) {
            unsigned internal=0;
            for(auto bank:_nesHistoryBanks)if(bank && esp_ptr_internal(bank))++internal;
            Serial.printf("NES video history: %u bytes; %u/%u internal banks; internal free %u; largest %u\n",
                unsigned(stateBytes),internal,unsigned(NesViewport::HISTORY_BANKS),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
        }
    }
    // Manual cleaning is confined to the game rectangle.
    // The panel still scans all physical rows, with neutral data everywhere else.
    if (clean || !smooth) {
        stopVideo();
        if (!_ready) return false;
        if (_gameImage) {
            if (_nesGame) NesViewport::blitPacked(getBuffer(), _gameImage, rotation);
            else GameViewport::blitPacked(getBuffer(), _gameImage, rotation);
        }
        copyRegion(rect);
    }
    if (clean) {
        checkResult(_panel.fullUpdate(CLEAR_SLOW, true, &rect));
        if (!_ready) return false;
        _videoReset = true;
    }
    if (smooth) {
        // Stop safely before arbitrary app code can run after a failed scan.
        _videoRunning = true;
        checkResult(_panel.videoScan(rect, _gameImage, _videoHistory, _videoReset, rowRepeat, rotation==3,
                                    _nesHistoryBanks[0] ? _nesHistoryBanks : nullptr,NesViewport::HISTORY_BANK_ROWS));
        _videoReset = false;
    } else if (!clean) {
        checkResult(_panel.partialUpdate(true, rect.y, rect.y + rect.h - 1));
    }
    return _ready;
}

#endif
