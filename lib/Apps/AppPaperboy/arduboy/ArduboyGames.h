#pragma once
// The Arduboy games built into Inkade. To add one: copy its sources into a
// folder here, replace its Arduboy2 / Arduboy2Beep / EEPROM includes with
// ArduboyCompat.h, include the .ino from a .cpp inside its own namespace
// (see UltraHop/UltraHop.cpp) and list it in ArduboyGames.cpp.
struct ArduboyGame {
    const char* title;  // shown in the game list
    void (*setup)();
    void (*loop)();
};
constexpr unsigned ARDUBOY_GAME_COUNT = 1;
extern const ArduboyGame ARDUBOY_GAMES[ARDUBOY_GAME_COUNT];
