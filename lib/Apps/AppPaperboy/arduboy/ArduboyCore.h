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
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY)
int inkArduboyFind(const char* path);  // -1 when no built-in game has it
#else
static inline int inkArduboyFind(const char* path) { (void)path; return -1; }  // Inkade boards only
#endif
bool inkArduboyOpen(unsigned index);
void inkArduboyClose(void);
// Runs one game frame. Inkade buttons (Game Boy bits): A jumps as the
// Arduboy's B (right-hand) button and B is the Arduboy's A. Writes the
// 128x64 screen as shades (0 white, 3 black) centered in the 160x144
// Game Boy frame `shades`.
bool inkArduboyFrame(uint16_t buttons, uint8_t* shades);
bool inkArduboyImage(uint8_t* shades);
uint8_t* inkArduboyRam(size_t* bytes);  // the 1 KB EEPROM
unsigned inkArduboyPeriod(void);
unsigned inkArduboyToneHz(void);  // the beeper's note now, 0 when silent
const char* inkArduboyName(void);
#ifdef __cplusplus
}
#endif
