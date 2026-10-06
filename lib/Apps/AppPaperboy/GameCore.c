#if defined(BOARD_LILYGO_T5S3_PRO)
// InkDeck adapter for MIT-licensed Peanut-GB; not copied from Paperboy's glue.
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include "peanut_gb.h"
#include "GameCore.h"
#include <setjmp.h>
#include <stdio.h>
#include <esp_heap_caps.h>

static struct gb_s* core;
static const uint8_t* romData;
static size_t romSize, ramSize;
static uint8_t *cartRam, *pixels;
static bool dirty;
static char errorText[80];
static jmp_buf failure;
static uint8_t readRom(struct gb_s* gb, uint_fast32_t address) {
    return address < romSize ? romData[address] : 0xff;
}
static uint8_t readRam(struct gb_s* gb, uint_fast32_t address) {
    return address < ramSize ? cartRam[address] : 0xff;
}
static void writeRam(struct gb_s* gb, uint_fast32_t address, uint8_t value) {
    if (address < ramSize && cartRam[address] != value) { cartRam[address] = value; dirty = true; }
}
static void fail(struct gb_s* gb, enum gb_error_e code, uint16_t address) {
    snprintf(errorText, sizeof(errorText), "Emulator error %d at %04X", (int)code, address);
    longjmp(failure, 1); // Unwind only this C adapter, never across C++ objects.
}
static void drawLine(struct gb_s* gb, const uint8_t* line, uint_fast8_t y) {
    if (pixels && y < 144) for (unsigned x = 0; x < 160; ++x) pixels[y * 160 + x] = line[x] & 3;
}
void inkGameClose(void) {
    free(core); core = NULL;
    free(cartRam); cartRam = NULL;
    ramSize = 0; pixels = NULL; dirty = false;
}
bool inkGameOpen(const uint8_t* rom, size_t bytes) {
    inkGameClose();
    errorText[0] = 0;
    if (bytes < 0x150 || bytes > 4 * 1024 * 1024 || rom[0x143] == 0xc0 ||
        rom[0x148] > 7 || bytes < (32768U << rom[0x148])) {
        snprintf(errorText, sizeof(errorText), "Use a DMG-compatible .gb ROM (max 4MB)"); return false;
    }
    core = heap_caps_calloc(1, sizeof(*core), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!core) { snprintf(errorText, sizeof(errorText), "Not enough emulator memory"); return false; }
    romData = rom; romSize = bytes;
    if (setjmp(failure)) return false;
    int result = gb_init(core, readRom, readRam, writeRam, fail, NULL);
    if (result != GB_INIT_NO_ERROR) {
        snprintf(errorText, sizeof(errorText), "Unsupported/invalid ROM (error %d)", result); return false;
    }
    // The first release deliberately excludes RTC cartridges rather than
    // silently losing their clock state. SRAM-only cartridges are persisted.
    if (rom[0x147] == 0x0f || rom[0x147] == 0x10) {
        snprintf(errorText, sizeof(errorText), "RTC cartridges not supported in this preview"); return false;
    }
    if (gb_get_save_size_s(core, &ramSize) != 0 || ramSize > 128 * 1024) {
        snprintf(errorText, sizeof(errorText), "Unsupported save RAM size"); return false;
    }
    if (ramSize) {
        cartRam = heap_caps_calloc(1, ramSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!cartRam) { snprintf(errorText, sizeof(errorText), "Not enough save memory"); return false; }
    }
    gb_init_lcd(core, drawLine);
    core->direct.joypad = 0xff;
    core->direct.frame_skip = 0;
    return true;
}
bool inkGameFrame(uint8_t buttons, uint8_t* shades) {
    if (!core || setjmp(failure)) return false;
    pixels = shades;
    core->direct.joypad = ~buttons;
    gb_run_frame(core);
    return true;
}
uint8_t* inkGameRam(size_t* size) { *size = ramSize; return cartRam; }
bool inkGameDirty(void) { return dirty; }
void inkGameSaved(void) { dirty = false; }
const char* inkGameError(void) { return errorText; }
#endif
