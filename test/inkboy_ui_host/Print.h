#pragma once
#include <stdint.h>
#include <stddef.h>
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    size_t print(const char* s) { size_t n=0; while(*s) n+=write(*s++); return n; }
};
