#pragma once

#include <Arduino.h>

class SoundMgr {
public:
    static SoundMgr& getInstance();

    void init();
    void beep();
    void setEnabled(bool enabled, bool persist = true);
    bool isEnabled() const { return _enabled; }
    bool isSupported() const;

private:
    SoundMgr() = default;
    void save();

    bool _enabled = true;
    bool _initialized = false;
};
