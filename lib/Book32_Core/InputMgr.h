#pragma once

#include <Arduino.h>
#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include "Config.h"
#if BOOK32_HAS_TOUCH
#include "StickyTouch.h"
#endif

enum InputAction {
    INPUT_NONE,
    INPUT_NEXT,
    INPUT_PREV,
    INPUT_SELECT,
    INPUT_BACK
};

class InputMgr {
public:
    static InputMgr& getInstance();
    void init();
    void update();

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
    StickyTouch touch;
    bool _touchDown = false;
    bool _touchMoved = false;
    uint16_t _touchStartX = 0;
    uint16_t _touchStartY = 0;
    uint16_t _touchLastX = 0;
    uint16_t _touchLastY = 0;
    unsigned long _touchStartedAt = 0;
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
    static void staticClick(void* ptr);
    static void staticDoubleClick(void* ptr);
    static void staticLongPress(void* ptr);
    static void staticUp(void* ptr);
    static void staticDown(void* ptr);
};
