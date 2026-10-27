#pragma once

#include <Arduino.h>
#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include "Config.h"
#if defined(BOARD_LILYGO_T5S3_PRO)
#include "DebouncedPress.h"
#endif
#if BOOK32_HAS_TOUCH
#include "StickyTouch.h"
#endif

enum InputAction {
    INPUT_NONE,
    INPUT_NEXT,
    INPUT_PREV,
    INPUT_SELECT,
    INPUT_BACK,
    INPUT_HOME,
    INPUT_POWER_SLEEP
};

class InputMgr {
public:
    static InputMgr& getInstance();
    void init();
    void update();
    void prepareForSleep();
#if BOOK32_HAS_INKBOY
    // Every finger currently down, for held/multi-touch game controls.
    struct TouchPoint { uint16_t x, y; };
    uint8_t heldTouches(TouchPoint* points) const;
#endif
    // USB serial test commands, one per line:
    //   tap X Y          a single tap at portrait (X, Y)
    //   hold X Y MS      a finger held at (X, Y) for MS milliseconds (game controls)
    void pollSerialCommands();
    // Other lines go to this handler (e.g. Inkade's "set ..." tuning).
    using SerialHandler = std::function<void(const char*)>;
    void setSerialHandler(SerialHandler handler) { serialHandler = handler; }
    void clearSerialHandler() { serialHandler = nullptr; }

    using InputCallback = std::function<void(InputAction)>;
    using TouchCallback = std::function<void(uint16_t, uint16_t)>;
    void setCallback(InputCallback cb) { callback = cb; }
    void clearCallback() { callback = nullptr; }
    void setTouchCallback(TouchCallback cb) { touchCallback = cb; }
    void clearTouchCallback() { touchCallback = nullptr; }

private:
    InputMgr();
    OneButton btn;
#if defined(BOARD_SEEED_STICKY)
    OneButton btnUp;
    OneButton btnDown;
#endif
#if defined(PIN_BUTTON_PREV)
    OneButton btnPrev;
#endif
#if defined(PIN_BUTTON_HOME)
    OneButton btnHome;
#endif
#if BOOK32_HAS_TOUCH
    StickyTouch touch;
    bool _touchDown = false;
    bool _touchMoved = false;
    uint16_t _touchStartX = 0;
    uint16_t _touchStartY = 0;
    uint16_t _touchLastX = 0;
    uint16_t _touchLastY = 0;
    unsigned long _touchStartedAt = 0;
#endif
#if BOOK32_HAS_INKBOY
    TouchPoint _points[5] = {};
    TouchPoint _injected = {};
    volatile unsigned long _injectedUntil = 0;
    // Serial "pinch" and "drag": fingers that move, for testing gestures.
    struct Motion { int x0, y0, x1, y1, spread0, spread1; unsigned long start, ms; };
    Motion _motion = {};
    volatile bool _motionActive = false;
    volatile uint8_t _pointCount = 0;
    volatile unsigned long _pointsAt = 0;
#endif
    String _serialLine;
    SerialHandler serialHandler;
#if defined(BOARD_LILYGO_T5S3_PRO)
    DebouncedPress _frontlightButton;
    uint32_t _frontlightPolledAt = 0;
    void pollFrontlightButton();
    void mappedButton(bool front);
#endif
    InputCallback callback;
    TouchCallback touchCallback;
    TaskHandle_t _taskHandle = nullptr;
    bool _taskRunning = false;

    struct InputEvent {
        InputAction action;
        uint16_t x;
        uint16_t y;
        bool touch;
    };
    static const uint8_t QUEUE_SIZE = 8;
    volatile uint8_t _queueHead = 0;
    volatile uint8_t _queueTail = 0;
    InputEvent _queue[QUEUE_SIZE];
    portMUX_TYPE _queueMux = portMUX_INITIALIZER_UNLOCKED;

    void enqueueAction(InputAction action);
    void enqueueTouch(uint16_t x, uint16_t y);
    bool dequeueEvent(InputEvent& event);
    static void inputTask(void* parameter);
    void pollTouch();

    void onClick();
    void onDoubleClick();
    void onLongPress();
    void onUp();
    void onDown();
    void onHome();
    void onBack();
    static void staticClick(void* ptr);
    static void staticDoubleClick(void* ptr);
    static void staticLongPress(void* ptr);
    static void staticUp(void* ptr);
    static void staticDown(void* ptr);
    static void staticHome(void* ptr);
    static void staticBack(void* ptr);
};
