#include "InputMgr.h"
#include "BatteryMgr.h"
#include "DisplayMgr.h"
#include <cstdlib>

#if defined(BOARD_SEEED_STICKY)
InputMgr::InputMgr() : btn(PIN_BUTTON, true, true),
                       btnUp(PIN_BUTTON_UP, true, true),
                       btnDown(PIN_BUTTON_DOWN, true, true) {
#else
InputMgr::InputMgr() : btn(PIN_BUTTON, true, true) {
#endif
    callback = nullptr;
    touchCallback = nullptr;
}

InputMgr& InputMgr::getInstance() {
    static InputMgr instance;
    return instance;
}

void InputMgr::init() {
    btn.setDebounceMs(30);
    btn.setClickMs(100);
    btn.setPressMs(400);
    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);

#if defined(BOARD_SEEED_STICKY)
    btnUp.setDebounceMs(30);
    btnDown.setDebounceMs(30);
    btnUp.setClickMs(100);
    btnDown.setClickMs(100);
    btnUp.attachClick(staticUp, this);
    btnDown.attachClick(staticDown, this);
    touch.begin();
#endif

    if (!_taskHandle) {
        BaseType_t result = xTaskCreatePinnedToCore(
            inputTask, "InputPoll", 4096, this, 2, &_taskHandle, 1);
        _taskRunning = (result == pdPASS);
        if (!_taskRunning) {
            Serial.println("Input task failed to start; falling back to loop polling");
            _taskHandle = nullptr;
        }
    }
}

void InputMgr::update() {
    if (!_taskRunning) {
        btn.tick();
#if defined(BOARD_SEEED_STICKY)
        btnUp.tick();
        btnDown.tick();
        pollTouch();
#endif
    }

    InputEvent event;
    while (dequeueEvent(event)) {
        if (event.touch) {
            if (touchCallback) touchCallback(event.x, event.y);
        } else if (callback) {
            callback(event.action);
        }
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);
    while (true) {
        self->btn.tick();
#if defined(BOARD_SEEED_STICKY)
        self->btnUp.tick();
        self->btnDown.tick();
        self->pollTouch();
#endif
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void InputMgr::enqueueAction(InputAction action) {
    if (action == INPUT_NONE) return;
    portENTER_CRITICAL(&_queueMux);
    uint8_t nextHead = (_queueHead + 1) % QUEUE_SIZE;
    if (nextHead != _queueTail) {
        _queue[_queueHead] = {action, 0, 0, false};
        _queueHead = nextHead;
    }
    portEXIT_CRITICAL(&_queueMux);
}

void InputMgr::enqueueTouch(uint16_t x, uint16_t y) {
    portENTER_CRITICAL(&_queueMux);
    uint8_t nextHead = (_queueHead + 1) % QUEUE_SIZE;
    if (nextHead != _queueTail) {
        _queue[_queueHead] = {INPUT_NONE, x, y, true};
        _queueHead = nextHead;
    }
    portEXIT_CRITICAL(&_queueMux);
}

bool InputMgr::dequeueEvent(InputEvent& event) {
    bool hasEvent = false;
    portENTER_CRITICAL(&_queueMux);
    if (_queueTail != _queueHead) {
        event = _queue[_queueTail];
        _queueTail = (_queueTail + 1) % QUEUE_SIZE;
        hasEvent = true;
    }
    portEXIT_CRITICAL(&_queueMux);
    return hasEvent;
}

void InputMgr::staticClick(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onClick(); }
void InputMgr::staticDoubleClick(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onDoubleClick(); }
void InputMgr::staticLongPress(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onLongPress(); }
void InputMgr::staticUp(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onUp(); }
void InputMgr::staticDown(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onDown(); }

void InputMgr::onClick() {
    BatteryMgr::getInstance().resetIdleTimer();
#if defined(BOARD_SEEED_STICKY)
    enqueueAction(INPUT_SELECT);
#else
    enqueueAction(INPUT_NEXT);
#endif
}

void InputMgr::onDoubleClick() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_PREV);
}

void InputMgr::onLongPress() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_SELECT);
}

void InputMgr::onUp() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_PREV);
}

void InputMgr::onDown() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_NEXT);
}

void InputMgr::pollTouch() {
#if defined(BOARD_SEEED_STICKY)
    bool touching = false;
    uint16_t nativeX = 0;
    uint16_t nativeY = 0;
    if (!touch.readFrame(touching, nativeX, nativeY)) return;

    uint16_t x = _touchLastX;
    uint16_t y = _touchLastY;
    if (touching && !DisplayMgr::getInstance().mapNativeTouchToScreen(nativeX, nativeY, x, y)) return;

    if (touching) {
        if (!_touchDown) {
            _touchDown = true;
            _touchMoved = false;
            _touchStartX = _touchLastX = x;
            _touchStartY = _touchLastY = y;
            _touchStartedAt = millis();
        } else {
            _touchLastX = x;
            _touchLastY = y;
            if (abs(static_cast<int>(x) - static_cast<int>(_touchStartX)) > 24 ||
                abs(static_cast<int>(y) - static_cast<int>(_touchStartY)) > 24) {
                _touchMoved = true;
            }
        }
    } else if (_touchDown) {
        unsigned long held = millis() - _touchStartedAt;
        _touchDown = false;
        if (!_touchMoved && held < 1200) {
            BatteryMgr::getInstance().resetIdleTimer();
            enqueueTouch(_touchLastX, _touchLastY);
        }
    }
#endif
}
