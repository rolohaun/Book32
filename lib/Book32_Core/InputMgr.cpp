#include "InputMgr.h"
#include "BatteryMgr.h"
#include "DisplayMgr.h"
#include <cstdlib>
#if defined(BOARD_LILYGO_T5S3_PRO)
#include <Wire.h>
#include "LilygoControls.h"
#include "StickyGrayImage.h"
#endif
#include "AppMgr.h"
#include "GamepadMgr.h"

#if defined(BOARD_SEEED_STICKY)
InputMgr::InputMgr() : btn(PIN_BUTTON, true, true),
                       btnUp(PIN_BUTTON_UP, true, true),
                       btnDown(PIN_BUTTON_DOWN, true, true) {
#elif defined(PIN_BUTTON_HOME)
InputMgr::InputMgr() : btn(PIN_BUTTON, true, true),
                       btnPrev(PIN_BUTTON_PREV, true, true),
                       btnHome(PIN_BUTTON_HOME, true, true) {
#elif defined(PIN_BUTTON_PREV)
InputMgr::InputMgr() : btn(PIN_BUTTON, true, true),
                       btnPrev(PIN_BUTTON_PREV, true, true) {
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
#if defined(PIN_BUTTON_HOME)
    // TRMNL kit: KEY1 turns back a page, KEY2 returns to the main menu.
    btnPrev.setDebounceMs(30);
    btnHome.setDebounceMs(30);
    btnPrev.setClickMs(100);
    btnHome.setClickMs(100);
    btnPrev.attachClick(staticUp, this);
    btnHome.attachClick(staticHome, this);
#elif defined(PIN_BUTTON_PREV)
    // RLCD: BOOT turns back a page; held, it goes back. KEY alone still
    // reaches everything.
    btnPrev.setDebounceMs(30);
    btnPrev.setClickMs(100);
    btnPrev.setPressMs(400);
    btnPrev.attachClick(staticUp, this);
    btnPrev.attachLongPressStart(staticBack, this);
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

void InputMgr::pollSerialCommands() {
    while (Serial.available()) {
        const char ch = Serial.read();
        if (ch != '\n' && ch != '\r') {
            if (_serialLine.length() < 48) _serialLine += ch;
            continue;
        }
        if (_serialLine.isEmpty()) continue;
        int x = 0, y = 0, ms = 0;
        (void)x; (void)y; (void)ms;
        const char* line = _serialLine.c_str();
        if (GamepadMgr::getInstance().serialCommand(line)) {
        }
#if BOOK32_HAS_TOUCH
        else if (sscanf(line, "tap %d %d", &x, &y) == 2) {
            enqueueTouch(x, y);
            Serial.printf("Serial: tap %d %d\n", x, y);
        }
#endif
#if BOOK32_HAS_INKBOY
        else if (!strncmp(line, "pinch ", 6) || !strncmp(line, "drag ", 5)) {
            // pinch CX CY FROM TO MS: two fingers FROM..TO apart around (CX, CY).
            // drag X0 Y0 X1 Y1 MS: one finger from (X0, Y0) to (X1, Y1).
            Motion m = {};
            const bool pinch = line[0] == 'p';
            int ms2 = 0;
            const int n = pinch ? sscanf(line + 6, "%d %d %d %d %d", &m.x0, &m.y0, &m.spread0, &m.spread1, &ms2)
                                : sscanf(line + 5, "%d %d %d %d %d", &m.x0, &m.y0, &m.x1, &m.y1, &ms2);
            if (n == 5 && ms2 > 0) {
                if (pinch) { m.x1 = m.x0; m.y1 = m.y0; } else { m.spread0 = m.spread1 = -1; }
                m.ms = ms2;
                m.start = millis();
                _motion = m;
                _motionActive = true;
                Serial.printf("Serial: %s\n", line);
            }
        }
        else if (sscanf(line, "hold %d %d %d", &x, &y, &ms) == 3) {
            _injected = {uint16_t(x), uint16_t(y)};
            _injectedUntil = millis() + ms;
            Serial.printf("Serial: hold %d %d for %d ms\n", x, y, ms);
        }
#endif
        else if (!strncmp(line, "key ", 4)) {
            // Board-button actions, for testing without touching the device.
            const char* k = line + 4;
            const InputAction a = !strcmp(k, "next") ? INPUT_NEXT : !strcmp(k, "prev") ? INPUT_PREV
                                : !strcmp(k, "select") ? INPUT_SELECT : !strcmp(k, "back") ? INPUT_BACK
                                : !strcmp(k, "home") ? INPUT_HOME : INPUT_NONE;
            if (a != INPUT_NONE) enqueueAction(a);
            Serial.printf("Serial: key %s\n", k);
        }
#if defined(BOARD_WAVESHARE_RLCD42)
        else if (!strncmp(line, "lcd ", 4)) {
            // lcd CMD [DATA...], hex bytes: a raw ST7305 command.
            uint8_t bytes[12];
            size_t count = 0;
            for (char* p = const_cast<char*>(line + 4); *p && count < sizeof(bytes);) {
                char* end;
                const unsigned long v = strtoul(p, &end, 16);
                if (end == p) break;
                bytes[count++] = uint8_t(v);
                p = end;
            }
            if (count) DisplayMgr::getInstance().getDisplay().tune(bytes[0], bytes + 1, count - 1);
            Serial.printf("Serial: %s\n", line);
        }
#endif
        else if (!strcmp(line, "cover")) {
            _serialLine = "";
            Serial.printf("Serial: cover %s\n", BatteryMgr::getInstance().previewSleepCover() ? "drawn" : "unavailable");
            return;
        }
#if defined(BOARD_LILYGO_T5S3_PRO)
        else if (!strcmp(line, "gray ramp")) {
            // The 16 native gray levels as labelled bands, 0 (top) to 15,
            // for measuring the panel's tone response. Each band's ends stay
            // white, a local reference that cancels uneven lighting.
            _serialLine = "";
            Book32Display& display = DisplayMgr::getInstance().getDisplay();
            constexpr int LABEL_W = 48, LEVELS = StickyGrayImage::LILYGO_TONE_COUNT;
            const int bandH = display.height() / LEVELS, w = display.width() - LABEL_W;
            uint8_t* gray = static_cast<uint8_t*>(ps_malloc(w * bandH * LEVELS));
            if (!gray) return;
            display.setFullWindow();
            display.fillScreen(GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.setTextSize(2);
            for (int i = 0; i < LEVELS; ++i) {
                for (int row = 0; row < bandH; ++row) {
                    uint8_t* line = gray + (i * bandH + row) * w;
                    memset(line, 255, w);
                    memset(line + w / 5, StickyGrayImage::LILYGO_TONES[i].lightness, w * 3 / 5);
                }
                display.setCursor(8, i * bandH + bandH / 2 - 7);
                display.print(i);
            }
            display.setTextSize(1);
            const bool drawn = display.drawGrayCover(gray, LABEL_W, 0, w, bandH * LEVELS);
            free(gray);
            Serial.printf("Serial: gray ramp %s\n", drawn ? "drawn" : "unavailable");
            return;
        }
#endif
        else if (!strcmp(line, "sleep")) {
            _serialLine = "";
            Serial.println("Serial: sleep");
            BatteryMgr::getInstance().enterIdleSleep();
            return;
        }
        else if (serialHandler) serialHandler(line);
        _serialLine = "";
    }
}

void InputMgr::update() {
    pollSerialCommands();
    // Bluetooth gamepad: menu actions for new presses (games read it raw).
    GamepadMgr::getInstance().pollActions([this](int action) {
        BatteryMgr::getInstance().resetIdleTimer();
        switch (action) {
        case 1: enqueueAction(INPUT_NEXT); break;
        case 2: enqueueAction(INPUT_PREV); break;
        case 3: enqueueAction(INPUT_SELECT); break;
        case 4: enqueueAction(INPUT_BACK); break;
        case 5: enqueueAction(INPUT_HOME); break;
        }
    });
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
#if defined(PIN_BUTTON_PREV)
        btnPrev.tick();
#endif
#if defined(PIN_BUTTON_HOME)
        btnHome.tick();
#endif
#if BOOK32_HAS_TOUCH
        pollTouch();
#endif
    }

    InputEvent event;
    while (dequeueEvent(event)) {
        // Home (LILYGO front key, gamepad Home) returns to the main menu.
        if (!event.touch && event.action == INPUT_HOME) {
            auto& apps = AppMgr::getInstance();
            if (apps.getCurrentApp() && strcmp(apps.getCurrentApp()->getName(), "Main Menu") != 0)
                apps.switchTo(0);
            continue;
        }
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
#if defined(PIN_BUTTON_PREV)
        self->btnPrev.tick();
#endif
#if defined(PIN_BUTTON_HOME)
        self->btnHome.tick();
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
void InputMgr::staticHome(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onHome(); }
void InputMgr::staticBack(void* ptr) { if (ptr) static_cast<InputMgr*>(ptr)->onBack(); }

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

void InputMgr::onHome() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_HOME);
}

void InputMgr::onBack() {
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_BACK);
}

void InputMgr::pollTouch() {
#if BOOK32_HAS_TOUCH
    bool touching = false;
    uint16_t nativeX = 0;
    uint16_t nativeY = 0;
#if BOOK32_HAS_INKBOY
    // LILYGO polls on the display-owning loop (all H752-01 peripherals share
    // Wire); Sticky polls from the input task. Both keep every held finger.
    uint16_t nx[5], ny[5];
    uint8_t count = 0;
    bool homePressed = false;
    if (!touch.readPoints(nx, ny, count, homePressed)) return;
    _pointCount = 0;
    _pointsAt = millis();
#if defined(BOARD_LILYGO_T5S3_PRO)
    if (homePressed) {
        _touchDown = false; // Cancel any pending screen tap; bezel is separate.
        mappedButton(true);
        return;
    }
#endif
    uint8_t kept = 0;
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t sx, sy;
        if (DisplayMgr::getInstance().mapNativeTouchToScreen(nx[i], ny[i], sx, sy))
            _points[kept++] = {sx, sy};
    }
    _pointCount = kept;
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

#endif

#if BOOK32_HAS_INKBOY
uint8_t InputMgr::heldTouches(TouchPoint* points) const {
    uint8_t count = millis() - _pointsAt > 250 ? 0 : _pointCount;
    for (uint8_t i = 0; i < count; ++i) points[i] = _points[i];
    if (count < 5 && long(_injectedUntil - millis()) > 0) points[count++] = _injected;
    if (_motionActive && count < 4) {
        const Motion& m = _motion;
        const unsigned long elapsed = millis() - m.start;
        if (elapsed <= m.ms) {
            const float t = float(elapsed) / m.ms;
            const int x = m.x0 + int((m.x1 - m.x0) * t), y = m.y0 + int((m.y1 - m.y0) * t);
            if (m.spread0 >= 0) {
                const int half = (m.spread0 + int((m.spread1 - m.spread0) * t)) / 2;
                points[count++] = {uint16_t(max(0, x - half)), uint16_t(y)};
                points[count++] = {uint16_t(x + half), uint16_t(y)};
            } else {
                points[count++] = {uint16_t(x), uint16_t(y)};
            }
        }
    }
    return count;
}
#endif
