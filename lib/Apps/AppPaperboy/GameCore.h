#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool inkGameOpen(const uint8_t* rom, size_t bytes);
void inkGameClose(void);
bool inkGameFrame(uint8_t buttons, uint8_t* shades);
uint8_t* inkGameRam(size_t* size);
bool inkGameDirty(void);
void inkGameSaved(void);
const char* inkGameError(void);
#ifdef __cplusplus
}
#endif
