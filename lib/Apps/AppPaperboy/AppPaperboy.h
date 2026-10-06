#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include "BaseApp.h"
#include <atomic>
#include <vector>
#include <freertos/semphr.h>

class AppPaperboy : public App {
public:
    const char* getName() override { return "Paperboy"; }
    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override { _redraw = true; }
private:
    std::vector<String> _roms;
    unsigned _page = 0;
    bool _redraw = true;
    String _message, _savePath;
    uint8_t *_rom = nullptr, *_latest = nullptr, *_snapshot = nullptr, *_workFrame = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    std::atomic<bool> _playing{false}, _frameReady{false}, _failed{false};
    std::atomic<uint8_t> _buttons{0};
    std::atomic<unsigned> _frames{0};
    unsigned long _lastClean = 0, _lastStats = 0;
    unsigned _updates = 0;
    static void run(void* self);
    void scan();
    void openBook(const String& path);
    bool save();
    void back();
    void tap(uint16_t x, uint16_t y);
    void paintGame();
};
#endif
