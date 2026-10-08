#pragma once
#include <stdint.h>

// One event per debounced press. Never turn a failed I2C read or a button
// already held at startup into an event. Unsigned subtraction handles millis()
// wraparound; callers supply successful samples rather than blocking delays.
class DebouncedPress {
public:
    bool update(bool valid, bool pressed, uint32_t now) {
        if (!valid) {
            _candidate = _stable;
            _since = now;
            return false;
        }
        if (!_initialized) {
            _initialized = true;
            _candidate = _stable = pressed;
            _armed = !pressed;
            _since = now;
            return false;
        }
        if (pressed != _candidate) {
            _candidate = pressed;
            _since = now;
        }
        if (_candidate == _stable || uint32_t(now - _since) < 30) return false;
        _stable = _candidate;
        if (!_stable) { _armed = true; return false; }
        const bool emit = _armed;
        _armed = false;
        return emit;
    }
private:
    bool _initialized = false, _candidate = false, _stable = false, _armed = false;
    uint32_t _since = 0;
};
