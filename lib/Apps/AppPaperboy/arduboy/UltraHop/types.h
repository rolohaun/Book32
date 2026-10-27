// Types used in function signatures. They live in a header so the Arduino
// builder's auto-generated prototypes can see them.
#pragma once
#include <Arduino.h>

enum class Mode : uint8_t { Title, Intro, Play, Dying, Clear, GameOver, Win };

enum : uint8_t { E_NONE, E_WALKER, E_SPIKY, E_BERRY, E_FLYER };
enum : uint8_t { S_ACTIVE, S_SQUASHED, S_RISING };

struct Entity {
  int16_t x, y;  // sprite top-left, sub units
  int8_t vx, vy;
  uint8_t type, state, timer;
};
