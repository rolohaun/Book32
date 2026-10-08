#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool inkGameOpen(const uint8_t* rom, size_t bytes);
typedef bool (*InkRomRead)(void* context, size_t offset, uint8_t* out, size_t bytes);
bool inkGameOpenBanked(const uint8_t* header, size_t bytes, InkRomRead read, void* context);
void inkGameClose(void);
bool inkGameFrame(uint8_t buttons, uint8_t* shades);
uint8_t* inkGameRam(size_t* size);
bool inkGameDirty(void);
void inkGameSaved(void);
const char* inkGameError(void);
bool inkGameIsColor(void);
bool inkGameHasRtc(void);
#define INK_RTC_STATE_BYTES 160
bool inkGameRtcExport(uint8_t state[INK_RTC_STATE_BYTES]);
bool inkGameRtcImport(const uint8_t state[INK_RTC_STATE_BYTES]);
// InkDeck/CrankBoy v6 snapshots: includes CPU, memory, mapper, RTC and LCD.
// Call only while the emulation core is locked. Buffers must be 4-byte aligned.
size_t inkGameStateSize(void);
bool inkGameStateExport(void* state, size_t size);
bool inkGameStateImport(const void* state, size_t size);
bool inkGameImage(uint8_t* shades);
bool inkGameRestart(void);
#ifdef __cplusplus
}
#endif
