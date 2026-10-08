#pragma once
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class LilygoControls {
public:
    enum Action { LIGHT, HOME, BACK, NO_ACTION, ACTION_COUNT };
    static LilygoControls& instance();
    void init();
    void apply(); // GPIO/PWM is touched only from the main/display loop.
    bool save(int brightness, int front, int side);
    int brightness() const { return _config.load() & 255; }
    Action frontAction() const { return Action((_config.load() >> 8) & 255); }
    Action sideAction() const { return Action((_config.load() >> 16) & 255); }
    static const char* actionName(Action action);
private:
    std::atomic<uint32_t> _config{100}; // Both buttons default to Light.
    uint32_t _applied = UINT32_MAX;
    SemaphoreHandle_t _mutex = nullptr;
};
#endif
