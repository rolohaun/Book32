#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
class __FlashStringHelper;
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
    size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
    size_t print(const char* s) { return write(s); }
    size_t print(const __FlashStringHelper* s) { return write((const char*)s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(long v, int base = 10) { char b[24]; snprintf(b, sizeof b, base == 16 ? "%lx" : "%ld", v); return write(b); }
    size_t print(int v, int base = 10) { return print((long)v, base); }
    size_t print(unsigned v, int base = 10) { return print((long)v, base); }
    size_t print(unsigned long v, int base = 10) { return print((long)v, base); }
    size_t print(uint8_t v, int base = 10) { return print((long)v, base); }
    size_t print(int16_t v, int base = 10) { return print((long)v, base); }
    size_t print(uint16_t v, int base = 10) { return print((long)v, base); }
};
