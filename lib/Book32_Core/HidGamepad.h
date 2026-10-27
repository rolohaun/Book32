#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Bluetooth LE gamepads describe their input reports with a HID report
// descriptor (the "report map"). This parses one and decodes input reports into
// a fixed set of buttons. Hardware-free, so host tests can exercise it.
//
// Bluetooth LE keyboards (such as the Rii i8+ mini keyboard) decode into the
// same buttons, so they drive menus, page turns and games like a pad: see
// keyButton(). Relative inputs (a keyboard's touchpad or a mouse) are ignored.
namespace HidGamepad {

// Decoded state. D-pad bits match Inkade's Game Boy button mask.
enum : uint16_t {
    PAD_A = 0x0001, PAD_B = 0x0002, PAD_SELECT = 0x0004, PAD_START = 0x0008,
    PAD_RIGHT = 0x0010, PAD_LEFT = 0x0020, PAD_UP = 0x0040, PAD_DOWN = 0x0080,
    PAD_X = 0x0100, PAD_Y = 0x0200, PAD_L = 0x0400, PAD_R = 0x0800,
    PAD_HOME = 0x1000, PAD_L2 = 0x2000, PAD_R2 = 0x4000,
};

// One input value inside a report.
struct Field {
    uint8_t reportId;
    uint16_t bitOffset;  // after the report ID byte
    uint8_t bitSize;
    uint16_t usagePage, usage;
    int32_t logicalMin, logicalMax;
};

constexpr int MAX_FIELDS = 64;
constexpr uint16_t PAGE_DESKTOP = 0x01, PAGE_BUTTON = 0x09, PAGE_CONSUMER = 0x0C, PAGE_SIMULATION = 0x02;
constexpr uint16_t PAGE_KEYBOARD = 0x07;
constexpr uint16_t USAGE_X = 0x30, USAGE_Y = 0x31, USAGE_HAT = 0x39;
constexpr uint16_t USAGE_DPAD_UP = 0x90, USAGE_DPAD_DOWN = 0x91, USAGE_DPAD_RIGHT = 0x92, USAGE_DPAD_LEFT = 0x93;

// Gamepad button numbers (HID button page usages, 1-based) for each pad
// button. Most Bluetooth LE gamepads in Android/"gamepad" mode, and Xbox
// controllers, use: 1 A, 2 B, 4 X, 5 Y, 7 L, 8 R, 9 L2, 10 R2, 11 Select,
// 12 Start, 13 Home.
struct ButtonMap { uint8_t a, b, x, y, l, r, select, start, home, l2, r2; };
constexpr ButtonMap ANDROID_MAP = {1, 2, 4, 5, 7, 8, 11, 12, 13, 9, 10};

// Keyboard keys (HID keyboard page usages) as pad buttons, after common
// emulator layouts: arrows move, X / Space are A, Z is B, Enter is Start,
// Backspace / right Shift are Select (Back in menus), Esc / Home pause.
// Page Up / Down turn pages. 0 for keys with no button.
inline uint16_t keyButton(uint16_t key) {
    switch (key) {
    case 0x4F: case 0x4E: return PAD_RIGHT;           // Right arrow, Page Down
    case 0x50: case 0x4B: return PAD_LEFT;            // Left arrow, Page Up
    case 0x51: return PAD_DOWN;
    case 0x52: return PAD_UP;
    case 0x1B: case 0x2C: return PAD_A;               // X, Space
    case 0x1D: return PAD_B;                          // Z
    case 0x16: return PAD_X;                          // S
    case 0x04: return PAD_Y;                          // A
    case 0x14: return PAD_L;                          // Q
    case 0x1A: return PAD_R;                          // W
    case 0x28: case 0x58: return PAD_START;           // Enter, keypad Enter
    case 0x2A: case 0xE5: return PAD_SELECT;          // Backspace, right Shift
    case 0x29: case 0x4A: return PAD_HOME;            // Esc, Home
    }
    return 0;
}

// Keyboard keys held in a report (HID usages: letters 0x04.., modifiers
// 0xE0..0xE7), and the pad buttons that came from everything but keys.
struct Keys {
    static constexpr int MAX = 8;
    uint8_t codes[MAX];
    uint8_t count = 0;
    uint16_t otherPad = 0;
    void add(uint8_t code) {
        if (!code || code == 1 || has(code) || count >= MAX) return;  // 1: too many keys
        codes[count++] = code;
    }
    bool has(uint8_t code) const {
        for (int i = 0; i < count; ++i) if (codes[i] == code) return true;
        return false;
    }
};

// Short key name ("X", "Enter", "Left") for the Controls screen.
inline const char* keyName(uint8_t key, char* buffer) {
    if (key >= 0x04 && key <= 0x1D) { buffer[0] = char('A' + key - 0x04); buffer[1] = 0; return buffer; }
    if (key >= 0x1E && key <= 0x27) { buffer[0] = key == 0x27 ? '0' : char('1' + key - 0x1E); buffer[1] = 0; return buffer; }
    if (key >= 0x3A && key <= 0x45) { buffer[0] = 'F'; snprintf(buffer + 1, 3, "%u", unsigned(key - 0x39)); return buffer; }
    switch (key) {
    case 0x28: return "Enter";
    case 0x29: return "Esc";
    case 0x2A: return "Backspace";
    case 0x2B: return "Tab";
    case 0x2C: return "Space";
    case 0x2D: return "-";
    case 0x2E: return "=";
    case 0x2F: return "[";
    case 0x30: return "]";
    case 0x31: return "\\";
    case 0x33: return ";";
    case 0x34: return "'";
    case 0x35: return "`";
    case 0x36: return ",";
    case 0x37: return ".";
    case 0x38: return "/";
    case 0x39: return "Caps Lock";
    case 0x49: return "Insert";
    case 0x4A: return "Home";
    case 0x4B: return "Page Up";
    case 0x4C: return "Delete";
    case 0x4D: return "End";
    case 0x4E: return "Page Down";
    case 0x4F: return "Right";
    case 0x50: return "Left";
    case 0x51: return "Down";
    case 0x52: return "Up";
    case 0x58: return "Keypad Enter";
    case 0xE0: return "Left Ctrl";
    case 0xE1: return "Left Shift";
    case 0xE2: return "Left Alt";
    case 0xE3: return "Left GUI";
    case 0xE4: return "Right Ctrl";
    case 0xE5: return "Right Shift";
    case 0xE6: return "Right Alt";
    case 0xE7: return "Right GUI";
    }
    snprintf(buffer, 8, "Key %02X", key);
    return buffer;
}

class Descriptor {
public:
    Field fields[MAX_FIELDS];
    int count = 0;
    bool usesReportIds = false;

    bool parse(const uint8_t* data, size_t length) {
        count = 0;
        usesReportIds = false;
        struct Globals { uint16_t page; int32_t min, max; uint8_t size, id; uint16_t reportCount; };
        Globals g = {0, 0, 0, 0, 0, 0}, stack[4];
        int depth = 0;
        uint16_t usages[16]; int usageCount = 0;
        uint32_t usageMin = 0, usageMax = 0; bool range = false;
        uint16_t offsets[256] = {};  // input bits per report ID
        size_t i = 0;
        while (i < length) {
            const uint8_t prefix = data[i++];
            if (prefix == 0xFE) {  // long item: skip
                if (i + 1 >= length) return false;
                i += 2 + data[i];
                continue;
            }
            const uint8_t size = (prefix & 3) == 3 ? 4 : (prefix & 3);
            if (i + size > length) return false;
            uint32_t u = 0;
            for (uint8_t b = 0; b < size; ++b) u |= uint32_t(data[i + b]) << (8 * b);
            int32_t s = size == 1 ? int8_t(u) : size == 2 ? int16_t(u) : int32_t(u);
            i += size;
            const uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
            if (type == 1) {  // global
                switch (tag) {
                case 0x0: g.page = u; break;
                case 0x1: g.min = s; break;
                case 0x2: g.max = (g.min >= 0 && s < 0) ? int32_t(u) : s; break;  // unsigned max
                case 0x7: g.size = u; break;
                case 0x8: g.id = u; usesReportIds = true; break;
                case 0x9: g.reportCount = u; break;
                case 0xA: if (depth < 4) stack[depth++] = g; break;
                case 0xB: if (depth > 0) g = stack[--depth]; break;
                }
            } else if (type == 2) {  // local
                if (tag == 0x0 && usageCount < 16) usages[usageCount++] = uint16_t(u);
                if (tag == 0x1) { usageMin = u & 0xFFFF; range = true; }
                if (tag == 0x2) { usageMax = u & 0xFFFF; range = true; }
            } else if (type == 0) {  // main
                if (tag == 0x8) {  // input
                    uint16_t& offset = offsets[g.id];
                    // Relative values (mouse or touchpad motion) are skipped:
                    // read as a stick they would press the d-pad.
                    const bool constant = u & 1, relative = u & 4;
                    for (uint16_t n = 0; n < g.reportCount; ++n) {
                        if (!constant && !relative && count < MAX_FIELDS && g.size && g.size <= 32) {
                            uint16_t usage = 0;
                            if (range && usageMin + n <= usageMax) usage = usageMin + n;
                            else if (usageCount) usage = usages[n < usageCount ? n : usageCount - 1];
                            else if (range) usage = usageMax;
                            fields[count++] = {g.id, offset, g.size, g.page, usage, g.min, g.max};
                        }
                        offset += g.size;
                    }
                }
                usageCount = 0; range = false; usageMin = usageMax = 0;
            }
        }
        return count > 0;
    }
};

inline uint32_t bits(const uint8_t* data, size_t length, uint16_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t b = 0; b < size; ++b) {
        const uint16_t bit = offset + b;
        if (bit / 8 >= length) break;
        if (data[bit / 8] & (1 << (bit % 8))) value |= 1u << b;
    }
    return value;
}

// Decodes one input report (`data` excludes the report ID, which BLE carries
// in the characteristic's Report Reference instead). Returns the pad buttons,
// keyboard keys included (keyButton). `keys` receives the keys themselves.
inline uint16_t decode(const Descriptor& d, uint8_t reportId, const uint8_t* data, size_t length,
                       const ButtonMap& map = ANDROID_MAP, uint32_t* rawButtons = nullptr,
                       Keys* keys = nullptr) {
    uint16_t pad = 0, keyPad = 0;
    uint32_t buttons = 0;
    for (int f = 0; f < d.count; ++f) {
        const Field& field = d.fields[f];
        if (d.usesReportIds && field.reportId != reportId) continue;
        uint32_t raw = bits(data, length, field.bitOffset, field.bitSize);
        int32_t value = raw;
        if (field.logicalMin < 0 && field.bitSize < 32 && (raw & (1u << (field.bitSize - 1))))
            value = int32_t(raw | (~0u << field.bitSize));  // sign-extend
        if (field.usagePage == PAGE_BUTTON) {
            if (value && field.usage >= 1 && field.usage <= 32) buttons |= 1u << (field.usage - 1);
        } else if (field.usagePage == PAGE_DESKTOP && field.usage == USAGE_HAT) {
            // Hat: logicalMin = up, then clockwise in eighths; out of range = centred.
            const int32_t h = value - field.logicalMin;
            const int32_t span = field.logicalMax - field.logicalMin + 1;
            if (h >= 0 && h < span) {
                const int dir = span == 4 ? h * 2 : h;  // 4-way hats count in quarters
                static const uint16_t HAT[8] = {PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_RIGHT | PAD_DOWN,
                                                PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_LEFT | PAD_UP};
                if (dir >= 0 && dir < 8) pad |= HAT[dir];
            }
        } else if (field.usagePage == PAGE_DESKTOP && (field.usage == USAGE_X || field.usage == USAGE_Y)) {
            // Left stick (or an axis-type d-pad): beyond 40% of travel counts.
            const int32_t lo = field.logicalMin, hi = field.logicalMax;
            if (hi > lo) {
                const int32_t mid = lo + (hi - lo) / 2, dead = (hi - lo) * 2 / 10;
                if (value < mid - dead) pad |= field.usage == USAGE_X ? PAD_LEFT : PAD_UP;
                if (value > mid + dead) pad |= field.usage == USAGE_X ? PAD_RIGHT : PAD_DOWN;
            }
        } else if (field.usagePage == PAGE_DESKTOP && value) {
            if (field.usage == USAGE_DPAD_UP) pad |= PAD_UP;
            if (field.usage == USAGE_DPAD_DOWN) pad |= PAD_DOWN;
            if (field.usage == USAGE_DPAD_RIGHT) pad |= PAD_RIGHT;
            if (field.usage == USAGE_DPAD_LEFT) pad |= PAD_LEFT;
        } else if (field.usagePage == PAGE_KEYBOARD && value) {
            // One bit per key (modifiers, or a bitmap keyboard), or an array
            // of the key codes held.
            const uint16_t key = field.bitSize == 1 ? field.usage : uint16_t(value);
            keyPad |= keyButton(key);
            if (keys && key <= 0xFF) keys->add(uint8_t(key));
        } else if (field.usagePage == PAGE_CONSUMER && value) {
            // Android-mode pads send Back / Home / Menu as consumer keys.
            if (field.bitSize > 1) {  // an array of usage codes
                if (value == 0x224) pad |= PAD_SELECT;       // AC Back
                if (value == 0x223 || value == 0x40) pad |= PAD_HOME;  // AC Home / Menu
            } else {
                if (field.usage == 0x224) pad |= PAD_SELECT;
                if (field.usage == 0x223 || field.usage == 0x40) pad |= PAD_HOME;
            }
        }
    }
    auto has = [&](uint8_t n) { return n && (buttons & (1u << (n - 1))); };
    if (has(map.a)) pad |= PAD_A;
    if (has(map.b)) pad |= PAD_B;
    if (has(map.x)) pad |= PAD_X;
    if (has(map.y)) pad |= PAD_Y;
    if (has(map.l)) pad |= PAD_L;
    if (has(map.r)) pad |= PAD_R;
    if (has(map.select)) pad |= PAD_SELECT;
    if (has(map.start)) pad |= PAD_START;
    if (has(map.home)) pad |= PAD_HOME;
    if (has(map.l2)) pad |= PAD_L2;
    if (has(map.r2)) pad |= PAD_R2;
    if (rawButtons) *rawButtons = buttons;
    if (keys) keys->otherPad = pad;
    return pad | keyPad;
}
}  // namespace HidGamepad
