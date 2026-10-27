#pragma once
#include "GameCore.h"
#include "RomFormat.h"
#ifdef __cplusplus
extern "C" {
#endif
bool inkConsoleOpen(InkSystem system,uint8_t* rom,size_t bytes,InkRomRead read,void* context);
void inkConsoleClose(void);
bool inkConsoleFrame(uint16_t buttons,uint8_t* shades);
bool inkConsoleImage(uint8_t* shades);
// The last frame's sound as 44.1 kHz mono (RLCD only); returns the samples.
size_t inkConsoleAudio(int16_t* out,size_t max);
bool inkConsoleRestart(void);
uint8_t* inkConsoleRam(size_t* size);
bool inkConsoleDirty(void);
void inkConsoleSaved(void);
bool inkConsoleHasRtc(void);
bool inkConsoleRtcExport(uint8_t* state);
bool inkConsoleRtcImport(const uint8_t* state);
const char* inkConsoleError(void);
const char* inkConsoleName(void);
InkSystem inkConsoleSystem(void);
unsigned inkConsolePeriod(void);
size_t inkConsoleStateSize(void);
bool inkConsoleStateExport(void* data,size_t bytes);
bool inkConsoleStateImport(const void* data,size_t bytes);
#ifdef __cplusplus
}
#endif
