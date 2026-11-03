// Minimal Arduino API for the host build of Inkade's Arduboy layer.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "Print.h"
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))
#define strlen_P strlen
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))
inline long random(long howBig) { return howBig ? rand() % howBig : 0; }
inline long random(long lo, long hi) { return lo + random(hi - lo); }
inline void randomSeed(unsigned long s) { srand(s); }
inline uint32_t esp_random() { return 12345; }
template <typename A, typename B> inline auto min(A a, B b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename A, typename B> inline auto max(A a, B b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#include <math.h>
#define radians(d) ((d) * 0.017453292519943295)
class String {
public:
    String() {}
    String(const char* s) : s_(s) {}
    String(const std::string& s) : s_(s) {}
    void replace(char a, char b) { for (auto& c : s_) if (c == a) c = b; }
    bool isEmpty() const { return s_.empty(); }
    unsigned length() const { return s_.size(); }
    char operator[](unsigned i) const { return s_[i]; }
    const char* c_str() const { return s_.c_str(); }
    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
private:
    std::string s_;
};
