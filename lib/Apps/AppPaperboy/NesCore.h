#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool inkNesOpenGame(uint8_t* rom,size_t bytes);
void inkNesCloseGame(void);
// shades may be NULL when the caller uses the native image (no thumbnail work).
bool inkNesFrame(uint16_t buttons,uint8_t* shades);
bool inkNesImage(uint8_t* shades);
// Borrowed 256x240 shade image; valid until the next core operation / close.
// Call while holding the core lock, then publish a separately packed snapshot.
const uint8_t* inkNesNativeImage(void);
// Borrow the indexed PPU image for fused palette conversion/packing. NULL
// after loading a save: use its native preview until a new frame is emulated.
const uint8_t* inkNesIndexedImage(unsigned* pitch,const uint8_t** shades);
bool inkNesRestart(void);
uint8_t* inkNesRam(size_t* bytes);
unsigned inkNesPeriod(void);
const char* inkNesError(void);
size_t inkNesStateSize(void);
bool inkNesStateExport(void* state,size_t bytes);
bool inkNesStateImport(const void* state,size_t bytes);
#ifdef __cplusplus
}
#endif
