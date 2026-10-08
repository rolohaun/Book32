#include "InputMgr.h"
#include "BatteryMgr.h"
#include "DisplayMgr.h"
#include <cstdlib>
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Wire.h>
#include "LilygoControls.h"
#include "AppMgr.h"
#endif

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
#if defined(BOARD_LILYGO_T5S3_PRO)
    LilygoControls::instance().init();
#endif
    btn.setDebounceMs(30);
    btn.setClickMs(100);
#if BOOK32_HAS_TOUCH
    // The Sticky shares confirm and power on GPIO4. A short release remains
    // Select/Back; holding for two seconds requests deep sleep.
    btn.setPressMs(2000);
#else
    btn.setPressMs(400);
#endif
    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);

#if defined(BOARD_SEEED_STICKY)
    btnUp.setDebounceMs(30);
    btnDown.setDebounceMs(30);
    btnUp.setClickMs(100);
    btnDown.setClickMs(100);
    btnUp.attachClick(staticUp, this);
    btnDown.attachClick(staticDown, this);
#endif
#if BOOK32_HAS_TOUCH
    touch.begin();
#endif

#if !defined(BOARD_LILYGO_T5S3_PRO)
    if (!_taskHandle) {
        BaseType_t result = xTaskCreatePinnedToCore(
            inputTask, "InputPoll", 4096, this, 2, &_taskHandle, 1);
        _taskRunning = (result == pdPASS);
        if (!_taskRunning) {
            Serial.println("Input task failed to start; falling back to loop polling");
            _taskHandle = nullptr;
        }
    }
#endif
}

void InputMgr::update() {
#if defined(BOARD_LILYGO_T5S3_PRO)
    LilygoControls::instance().apply();
    pollFrontlightButton();
#endif
    if (!_taskRunning) {
        btn.tick();
#if defined(BOARD_SEEED_STICKY)
        btnUp.tick();
        btnDown.tick();
#endif
#if BOOK32_HAS_TOUCH
        pollTouch();
#endif
    }

    InputEvent event;
    while (dequeueEvent(event)) {
#if defined(BOARD_LILYGO_T5S3_PRO)
        if (!event.touch && event.action == INPUT_HOME) {
            auto& apps = AppMgr::getInstance();
            if (apps.getCurrentApp() && strcmp(apps.getCurrentApp()->getName(), "Main Menu") != 0)
                apps.switchTo(0);
            continue;
        }
#endif
        if (!event.touch && event.action == INPUT_POWER_SLEEP) {
            BatteryMgr::getInstance().enterIdleSleep();
            return;
        }
        if (event.touch) {
            if (touchCallback) touchCallback(event.x, event.y);
        } else if (callback) {
            callback(event.action);
        }
    }
}

void InputMgr::prepareForSleep() {
    if (_taskHandle) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
    _taskRunning = false;
#if BOOK32_HAS_TOUCH
    touch.stop();
#endif
    clearCallback();
    clearTouchCallback();
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);
    while (true) {
        self->btn.tick();
#if defined(BOARD_SEEED_STICKY)
        self->btnUp.tick();
        self->btnDown.tick();
#endif
#if BOOK32_HAS_TOUCH
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
#if BOOK32_HAS_TOUCH
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
#if BOOK32_HAS_TOUCH
    enqueueAction(INPUT_POWER_SLEEP);
#else
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_SELECT);
#endif
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
#if BOOK32_HAS_TOUCH
    bool touching = false;
    uint16_t nativeX = 0;
    uint16_t nativeY = 0;
#if defined(BOARD_LILYGO_T5S3_PRO)
    // Poll on the display-owning loop: all H752-01 peripherals share Wire.
    uint16_t nx[5], ny[5];
    uint8_t count = 0;
    bool homePressed = false;
    if (!touch.readPoints(nx, ny, count, homePressed)) return;
    _pointCount = 0;
    _pointsAt = millis();
    if (homePressed) {
        _touchDown = false; // Cancel any pending screen tap; bezel is separate.
        mappedButton(true);
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t sx, sy;
        if (DisplayMgr::getInstance().mapNativeTouchToScreen(nx[i], ny[i], sx, sy))
            _points[_pointCount++] = {sx, sy};
    }
    touching = _pointCount > 0;
    if (touching) { nativeX = nx[0]; nativeY = ny[0]; }
#else
    if (!touch.readFrame(touching, nativeX, nativeY)) return;
#endif

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

#if defined(BOARD_LILYGO_T5S3_PRO)
void InputMgr::pollFrontlightButton() {
    const uint32_t now = millis();
    if (uint32_t(now - _frontlightPolledAt) < 20) return;
    _frontlightPolledAt = now;
    // Run only on the display-owning loop, never a second I2C task. FastEPD
    // already configures IO1_2 as input; do not touch its cached power outputs.
    Wire.beginTransmission(FRONTLIGHT_BUTTON_I2C_ADDRESS);
    Wire.write(FRONTLIGHT_BUTTON_INPUT_REGISTER);
    bool valid = Wire.endTransmission(false) == 0;
    bool pressed = false;
    if (valid) {
        valid = Wire.requestFrom(uint8_t(FRONTLIGHT_BUTTON_I2C_ADDRESS), uint8_t(1)) == 1;
        if (valid) pressed = (Wire.read() & FRONTLIGHT_BUTTON_MASK) == 0;
        else while (Wire.available()) Wire.read();
    }
    if (_frontlightButton.update(valid, pressed, now)) {
        mappedButton(false);
    }
}

void InputMgr::mappedButton(bool front) {
    const auto action = front ? LilygoControls::instance().frontAction() : LilygoControls::instance().sideAction();
    BatteryMgr::getInstance().resetIdleTimer();
    if (action == LilygoControls::HOME) enqueueAction(INPUT_HOME);
    else if (action == LilygoControls::BACK) enqueueAction(INPUT_BACK);
    else if (action == LilygoControls::LIGHT) {
        auto& display = DisplayMgr::getInstance().getDisplay();
        display.setFrontlight(!display.frontlightOn());
        Serial.printf("Frontlight: %s (%s)\n", display.frontlightOn() ? "on" : "off", front ? "front touch key" : "S3 switch");
    }
}

uint8_t InputMgr::heldTouches(TouchPoint* points) const {
    if (millis() - _pointsAt > 250) return 0;
    for (uint8_t i = 0; i < _pointCount; ++i) points[i] = _points[i];
    return _pointCount;
}
#endif
