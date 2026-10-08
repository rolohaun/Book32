#pragma once
// Platform boundary for CrankBoy's portable C core. No Playdate SDK/ARM code.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define CPU_VALIDATE 0
#define __section__(x)
#define __shell
#define FORCE_INLINE inline __attribute__((always_inline))
#define likely(x) (__builtin_expect(!!(x), 1))
#define unlikely(x) (__builtin_expect(!!(x), 0))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CRANK_MENU_DELTA_BINANGLE 256
#define CB_ASSERT(x) assert(x)
static FORCE_INLINE uint16_t inkRead16(const void* p) { const uint8_t* b=p; return b[0] | ((uint16_t)b[1]<<8); }
static FORCE_INLINE uint32_t inkRead32(const void* p) { const uint8_t* b=p; return b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24); }
static FORCE_INLINE void inkWrite16(void* p, uint16_t v) { uint8_t* b=p; b[0]=v; b[1]=v>>8; }
#define preferences_cgb_gamma 4 // Upstream index 4 = linear 1.0, not index 0 (0.6).
#define preferences_cgb_bias_auto 0
#define preferences_cgb_speed 0
// Emulate APU registers and sequencer, without a Playdate audio callback.
#define preferences_sound_mode 1
#define preferences_sample_rate 0
#define preferences_uncap_fps 0
#define kAccelerometer 1
#define kNone 0
enum cgb_support_e { NON_GB_SYSTEM, GB_SUPPORT_DMG, GB_SUPPORT_CGB, GB_SUPPORT_DMG_AND_CGB };
void* inkCrankAlloc(size_t bytes);
uint8_t* inkCrankRomBank(uint8_t* rom, unsigned bank, unsigned slot);
bool inkCrankIsPaged(void);
#define cb_malloc inkCrankAlloc
#define cb_free free
static inline void inkCrankLog(const char* fmt, ...) { (void)fmt; }
static inline void inkCrankPeripherals(int enabled) { (void)enabled; }
static inline void inkCrankAccelerometer(float* x, float* y, float* z) {
    if (x) *x=0; if (y) *y=0; if (z) *z=0;
}
static FORCE_INLINE uint8_t reverse_bits_u8(uint8_t b) {
    b = (b >> 4) | (b << 4);
    b = ((b & 0xcc) >> 2) | ((b & 0x33) << 2);
    return ((b & 0xaa) >> 1) | ((b & 0x55) << 1);
}
static FORCE_INLINE uint32_t reverse_bits_in_each_byte_conditional_u16(uint16_t b, bool reverse) {
    return reverse ? reverse_bits_u8(b) | ((uint32_t)reverse_bits_u8(b >> 8) << 8) : b;
}
