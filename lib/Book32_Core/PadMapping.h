#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "HidGamepad.h"

// Which gamepad button drives each Inkade game action: one to one. Each
// action has exactly one pad button (a HidGamepad::PAD_* bit), and no button
// serves two actions. Hardware-free for host tests.
namespace PadMapping {

// Game actions: the first eight in the Game Boy / NES button bit order.
enum Action : uint8_t { GAME_A, GAME_B, GAME_SELECT, GAME_START, GAME_RIGHT, GAME_LEFT, GAME_UP, GAME_DOWN,
                        PAUSE, ACTION_COUNT };

// Stored alongside the masks. Version 1 allowed several buttons per action;
// those records are refused, so such a device starts from the defaults.
constexpr uint8_t VERSION = 2;

struct Map {
    uint16_t masks[ACTION_COUNT];
};

// Each action on the pad button of the same name; Home pauses.
constexpr Map DEFAULTS = {{
    HidGamepad::PAD_A, HidGamepad::PAD_B,
    HidGamepad::PAD_SELECT, HidGamepad::PAD_START,
    HidGamepad::PAD_RIGHT, HidGamepad::PAD_LEFT, HidGamepad::PAD_UP, HidGamepad::PAD_DOWN,
    HidGamepad::PAD_HOME,
}};

// Every pad button that can be assigned, in display order.
constexpr uint16_t BUTTONS[] = {
    HidGamepad::PAD_A, HidGamepad::PAD_B, HidGamepad::PAD_X, HidGamepad::PAD_Y,
    HidGamepad::PAD_L, HidGamepad::PAD_R, HidGamepad::PAD_L2, HidGamepad::PAD_R2,
    HidGamepad::PAD_SELECT, HidGamepad::PAD_START, HidGamepad::PAD_HOME,
    HidGamepad::PAD_UP, HidGamepad::PAD_DOWN, HidGamepad::PAD_LEFT, HidGamepad::PAD_RIGHT,
};
constexpr int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
constexpr uint16_t ALL_BUTTONS = 0x7FFF;

inline const char* actionName(int action) {
    static const char* const NAMES[ACTION_COUNT] = {"A", "B", "Select", "Start", "Right", "Left", "Up", "Down",
                                                    "Pause menu"};
    return action >= 0 && action < ACTION_COUNT ? NAMES[action] : "";
}

inline const char* buttonName(uint16_t button) {
    using namespace HidGamepad;
    switch (button) {
    case PAD_A: return "A";
    case PAD_B: return "B";
    case PAD_X: return "X";
    case PAD_Y: return "Y";
    case PAD_L: return "L";
    case PAD_R: return "R";
    case PAD_L2: return "L2";
    case PAD_R2: return "R2";
    case PAD_SELECT: return "Select";
    case PAD_START: return "Start";
    case PAD_HOME: return "Home";
    case PAD_UP: return "Up";
    case PAD_DOWN: return "Down";
    case PAD_LEFT: return "Left";
    case PAD_RIGHT: return "Right";
    }
    return "?";
}

// "A, X", or "None", into `out`.
inline void describe(uint16_t mask, char* out, size_t size) {
    if (!size) return;
    out[0] = 0;
    size_t used = 0;
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        if (!(mask & BUTTONS[i])) continue;
        const int n = snprintf(out + used, size - used, "%s%s", used ? ", " : "", buttonName(BUTTONS[i]));
        if (n < 0 || size_t(n) >= size - used) { out[used] = 0; break; }  // whole names only
        used += n;
    }
    if (!used) snprintf(out, size, "None");
}

// The Game Boy / NES buttons (bit order as Action) held on the pad.
inline uint8_t gameButtons(const Map& map, uint16_t pad) {
    uint8_t buttons = 0;
    for (int action = GAME_A; action <= GAME_DOWN; ++action)
        if (pad & map.masks[action]) buttons |= 1 << action;
    return buttons;
}

// Opens the pause menu. Select + Start together always does, so a mapping
// can never lock the player inside a game.
inline bool pauses(const Map& map, uint16_t pad) {
    constexpr uint16_t SELECT_START = HidGamepad::PAD_SELECT | HidGamepad::PAD_START;
    return (pad & map.masks[PAUSE]) || (pad & SELECT_START) == SELECT_START;
}

// The first assignable button in `pressed` (in BUTTONS order), or 0.
inline uint16_t firstButton(uint16_t pressed) {
    for (int i = 0; i < BUTTON_COUNT; ++i)
        if (pressed & BUTTONS[i]) return BUTTONS[i];
    return 0;
}

// Puts `button` on the action. An action that already had it takes the
// action's old button instead, so the mapping stays one to one and complete.
// Returns that other action, or -1 when nothing swapped.
inline int assign(Map& map, int action, uint16_t button) {
    if (action < 0 || action >= ACTION_COUNT || !(button = firstButton(button))) return -1;
    const uint16_t previous = map.masks[action];
    if (previous == button) return -1;
    int swapped = -1;
    for (int other = 0; other < ACTION_COUNT; ++other)
        if (other != action && map.masks[other] == button) { map.masks[other] = previous; swapped = other; }
    map.masks[action] = button;
    return swapped;
}

inline bool isDefault(const Map& map) { return !memcmp(&map, &DEFAULTS, sizeof(Map)); }

// Stored form: VERSION, then each mask little-endian.
constexpr size_t STORED_BYTES = 1 + 2 * ACTION_COUNT;
inline void store(const Map& map, uint8_t* out) {
    out[0] = VERSION;
    for (int i = 0; i < ACTION_COUNT; ++i) { out[1 + 2 * i] = map.masks[i] & 0xFF; out[2 + 2 * i] = map.masks[i] >> 8; }
}
// False (and `map` untouched) for anything but a current, one-to-one record.
inline bool restore(Map& map, const uint8_t* in, size_t length) {
    if (length != STORED_BYTES || in[0] != VERSION) return false;
    Map loaded;
    uint16_t used = 0;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const uint16_t mask = uint16_t(in[1 + 2 * i] | (in[2 + 2 * i] << 8));
        if (mask != firstButton(mask) || !mask || (used & mask)) return false;  // exactly one, unshared
        used |= mask;
        loaded.masks[i] = mask;
    }
    map = loaded;
    return true;
}
}  // namespace PadMapping

// The same game actions on a Bluetooth keyboard: one key each (HID keyboard
// usage), any key. Kept apart from the pad mapping, so a keyboard and a
// gamepad each keep their own layout.
namespace KeyMapping {
using PadMapping::ACTION_COUNT;
constexpr uint8_t VERSION = 1;

struct Map {
    uint8_t keys[ACTION_COUNT];
};

// X is A and Z is B (emulator custom), Backspace Select, Enter Start,
// the arrows, and Esc pauses.
constexpr Map DEFAULTS = {{0x1B, 0x1D, 0x2A, 0x28, 0x4F, 0x50, 0x52, 0x51, 0x29}};

// The Game Boy / NES buttons (bit order as PadMapping::Action) held.
inline uint8_t gameButtons(const Map& map, const HidGamepad::Keys& held) {
    uint8_t buttons = 0;
    for (int action = PadMapping::GAME_A; action <= PadMapping::GAME_DOWN; ++action)
        if (held.has(map.keys[action])) buttons |= 1 << action;
    return buttons;
}

inline bool pauses(const Map& map, const HidGamepad::Keys& held) { return held.has(map.keys[PadMapping::PAUSE]); }

// Puts `key` on the action; an action that had it takes the old key.
// Returns that other action, or -1 when nothing swapped.
inline int assign(Map& map, int action, uint8_t key) {
    if (action < 0 || action >= ACTION_COUNT || !key) return -1;
    const uint8_t previous = map.keys[action];
    if (previous == key) return -1;
    int swapped = -1;
    for (int other = 0; other < ACTION_COUNT; ++other)
        if (other != action && map.keys[other] == key) { map.keys[other] = previous; swapped = other; }
    map.keys[action] = key;
    return swapped;
}

inline bool isDefault(const Map& map) { return !memcmp(&map, &DEFAULTS, sizeof(Map)); }

constexpr size_t STORED_BYTES = 1 + ACTION_COUNT;
inline void store(const Map& map, uint8_t* out) {
    out[0] = VERSION;
    memcpy(out + 1, map.keys, ACTION_COUNT);
}
// False (and `map` untouched) for anything but a current, one-to-one record.
inline bool restore(Map& map, const uint8_t* in, size_t length) {
    if (length != STORED_BYTES || in[0] != VERSION) return false;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (!in[1 + i]) return false;
        for (int j = 0; j < i; ++j) if (in[1 + i] == in[1 + j]) return false;
    }
    memcpy(map.keys, in + 1, ACTION_COUNT);
    return true;
}
}  // namespace KeyMapping
