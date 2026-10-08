#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#define PROGMEM
using String = std::string;
class __FlashStringHelper;
inline void yield() {}
inline float radians(float degrees) { return degrees * 0.017453292519943295f; }
