#pragma once
#include <stdint.h>
#include <stdio.h>
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void delay(int) {}
inline int constrain(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
struct TestSerial {
    template<typename... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
extern TestSerial Serial;
