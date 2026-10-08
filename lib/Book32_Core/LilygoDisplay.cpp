#include "LilygoDisplay.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Wire.h>
#include <driver/gpio.h>
#include <soc/soc_memory_types.h>
#include "GameViewport.h"
#include "NesViewport.h"
#include "GameVideoSelfTest.h"
namespace { constexpr uint8_t FRONTLIGHT_CHANNEL = 7; }

void LilygoDisplay::init(uint32_t, bool, uint16_t, bool) {
    _videoAvailable = gameVideoSelfTest();
    Serial.printf("Game video self-test (GB 3x, NES 2x, pulses, clipping, queued DMA, frame timing): %s\n",
                  _videoAvailable ? "PASS" : "FAIL");
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
    // Do not energize a panel using the older H752's incompatible pinout.
    Wire.beginTransmission(0x20);
    if (Wire.endTransmission() != 0) {
        Serial.println("LILYGO H752-01 expander missing; display disabled");
        return;
    }
    // Official H752-01 examples use the EPDiy V7 parallel/power mapping.
    _ready = _panel.initPanel(BB_PANEL_EPDIY_V7, 20000000) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setPanelSize(960, 540) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setMode(BB_MODE_1BPP) == BBEP_SUCCESS;
    if (_ready) clearScreen();
}

void LilygoDisplay::refresh(bool partial) {
    if (!_ready || !getBuffer() || !_panel.currentBuffer()) return;
    stopVideo();
    if (!_ready) return;
    // Both buffers are MSB-first, native-landscape, white=1.
    memset(_panel.currentBuffer(), 0xff, 960 * 540 / 8);
    for (int y = 0; y < LilygoLayout::CANVAS_H; ++y)
        memcpy(_panel.currentBuffer() + (y + LilygoLayout::INSET) * 120 + LilygoLayout::INSET / 8,
               getBuffer() + y * LilygoLayout::PITCH, LilygoLayout::PITCH);
    int result = partial ? _panel.partialUpdate(_gaming)
                         : _panel.fullUpdate(CLEAR_SLOW, false);
    checkResult(result);
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
