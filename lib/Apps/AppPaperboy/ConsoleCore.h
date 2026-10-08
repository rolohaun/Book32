#pragma once
#include "GameCore.h"
#include "RomFormat.h"
#ifdef __cplusplus
extern "C" {
#endif
bool inkConsoleOpen(InkSystem system,uint8_t* rom,size_t bytes,InkRomRead read,void* context);
void inkConsoleClose(void);
bool inkConsoleFrame(uint16_t buttons,uint8_t* shades);
// Only Genesis supports omitting pixel output; other cores still draw normally.
bool inkConsoleStep(uint16_t buttons,uint8_t* shades,bool render);
bool inkConsoleImage(uint8_t* shades);
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
