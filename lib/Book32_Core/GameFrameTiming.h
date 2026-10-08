#pragma once
#include <stdint.h>

namespace GameFrameTiming {
// Native DMG frame duration, rounded to microseconds, as in the existing core
// pacing. The panel may scan at up to 60 Hz; game speed must not follow jitter.
constexpr uint32_t PERIOD_US = 16743;
constexpr uint32_t MAX_GAP_US = 100000;

// Display-task-only clock. Each boundary requests work for the NEXT scan.
// Cumulative targets allow catch-up without discarding game simulation steps.
// Long maintenance pauses are rebased, not replayed as many unseen frames.
class Clock {
public:
    void reset(uint32_t produced, uint32_t period = PERIOD_US) {
        _period = period ? period : PERIOD_US;
        _target = produced + 1; // Prime an image before the opening clean.
        _started = false;
        _remainder = 0;
    }
    uint32_t target() const { return _target; }
    uint32_t boundary(int64_t now, bool maintenance = false) {
        const int64_t elapsed = now - _last;
        if (!_started || maintenance || elapsed < 0 || elapsed > MAX_GAP_US) {
            ++_target;
            _remainder = 0;
        } else {
            const uint32_t total = _remainder + static_cast<uint32_t>(elapsed);
            _target += total / _period;
            _remainder = total % _period;
        }
        _last = now;
        _started = true;
        return _target;
    }
private:
    uint32_t _target = 1, _remainder = 0, _period = PERIOD_US;
    int64_t _last = 0;
    bool _started = false;
};

// IDs identify complete frames, not changed pixels: a still scene can be fresh.
class Presentations {
public:
    uint32_t fresh = 0, repeated = 0, skipped = 0, maintenance = 0;
    void reset() { *this = Presentations(); }
    void clearCounts() { fresh = repeated = skipped = maintenance = 0; }
    void observe(uint32_t id, bool valid, bool isMaintenance) {
        if (isMaintenance) ++maintenance;
        if (!valid) return;
        const uint32_t delta = id - _last;
        if (!isMaintenance) {
            if (_valid && delta == 0) ++repeated;
            else {
                ++fresh;
                if (_valid && delta > 1) skipped += delta - 1;
            }
        }
        _last = id;
        _valid = true;
    }
private:
    uint32_t _last = 0;
    bool _valid = false;
};

inline bool selfTest() {
    Clock clock;
    clock.reset(0);
    if (clock.target() != 1 || clock.boundary(0) != 2) return false;
    // A 60 Hz panel does not speed the emulator up to 60 fps.
    for (int i = 1; i <= 10000; ++i) {
        const int64_t now = int64_t(i) * 16667;
        if (clock.boundary(now) != 2 + now / PERIOD_US) return false;
    }
    // NES PAL stays at 50 Hz on the same 60 Hz display scheduler.
    const uint32_t consolePeriods[] = {16667, 20000};
    for (uint32_t period : consolePeriods) {
        clock.reset(0,period);
        if (clock.boundary(0)!=2) return false;
        for (int i=1;i<=10000;++i) {
            int64_t now=int64_t(i)*16667;
            if (clock.boundary(now)!=2+now/period) return false;
        }
    }
    // Jittered/slower scan boundaries must preserve the same accumulated game
    // time, including zero-work and two-frame catch-up boundaries.
    clock.reset(0);
    if (clock.boundary(0) != 2) return false;
    int64_t now = 0;
    const uint32_t gaps[] = {16667, 17100, 18000, 33000, 16743, 5000};
    for (int i = 0; i < 10000; ++i) {
        now += gaps[i % 6];
        if (clock.boundary(now) != 2 + now / PERIOD_US) return false;
    }
    clock.reset(40);
    if (clock.boundary(1000) != 42 || clock.boundary(1000 + 2*PERIOD_US) != 44) return false;
    // Cleaning/UI stalls must not cause a catch-up storm.
    if (clock.boundary(1000000, true) != 45 || clock.boundary(2000000) != 46) return false;
    if (clock.boundary(2000000 + PERIOD_US) != 47) return false;
    clock.reset(UINT32_MAX - 1);
    if (clock.target() != UINT32_MAX || clock.boundary(0) != 0 ||
        clock.boundary(PERIOD_US) != 1) return false;
    Presentations stats;
    stats.observe(0, false, false);
    stats.observe(10, true, true);
    stats.observe(11, true, false);
    stats.observe(11, true, false);
    stats.observe(14, true, false);
    if (stats.fresh != 2 || stats.repeated != 1 || stats.skipped != 2 || stats.maintenance != 1) return false;
    stats.clearCounts();
    stats.observe(14, true, false); // Preserve continuity between log samples.
    if (stats.fresh || stats.skipped || stats.repeated != 1) return false;
    stats.reset();
    stats.observe(UINT32_MAX, true, false);
    stats.observe(0, true, false);
    return stats.fresh == 2 && stats.repeated == 0 && stats.skipped == 0;
}
}
