#pragma once
// Inkade's console API for the built-in Arduboy games (see ArduboyCompat.h).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
unsigned inkArduboyCount(void);
// "/roms/<Title_with_underscores>.arduboy": a path the game list can show.
const char* inkArduboyPath(unsigned index);
#if defined(BOARD_WAVESHARE_RLCD42)
int inkArduboyFind(const char* path);  // -1 when no built-in game has it
#else
static inline int inkArduboyFind(const char* path) { (void)path; return -1; }  // RLCD only
#endif
bool inkArduboyOpen(unsigned index);
void inkArduboyClose(void);
// Runs one game frame. Inkade buttons (Game Boy bits): A jumps as the
// Arduboy's B (right-hand) button and B is the Arduboy's A. Writes the
// 128x64 screen as shades (0 white, 3 black) at the start of `shades`.
bool inkArduboyFrame(uint16_t buttons, uint8_t* shades);
bool inkArduboyImage(uint8_t* shades);
uint8_t* inkArduboyRam(size_t* bytes);  // the 1 KB EEPROM
unsigned inkArduboyPeriod(void);
size_t inkArduboyAudio(int16_t* out, size_t max);
const char* inkArduboyName(void);
#ifdef __cplusplus
}
#endif
