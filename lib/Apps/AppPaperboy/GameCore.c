#if defined(BOARD_LILYGO_T5S3_PRO)
// CrankBoy's DMG and CGB interpreters, with InkDeck storage/display adapters.
#define PGB_IMPL
#include "crankboy/peanut_gb.h"
#include "GameCore.h"
#include "RomFormat.h"
#include <setjmp.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

uint8_t cgb_blend_stage = 0, cgb_gray_lum_min = 0, cgb_gray_lum_max = 93;
int8_t cgb_gray_bias = 0;
bool cgb_hist_active = false, cgb_contrast_active = false;
uint16_t cgb_thresh[3] = {70, 46, 23}, cgb_thresh_delta = 0;
intptr_t pgb_draw_reloc_offset = 0, pgb_rare_reloc_offset = 0, pgb_hle_reloc_offset = 0;
intptr_t pgb_apu_write_reloc_offset = 0, pgb_apu_sample_gen_reloc_offset = 0;
uint8_t* pgb_dirty_prev = NULL;
uint16_t* pgb_dirty_flags = NULL;
uint8_t pgb_dirty_skip = 0;

static gb_s* core;
static uint8_t *workRam, *videoRam, *lcd, *cartRam;
static size_t ramSize;
static char errorText[96];
static jmp_buf failure;
static int64_t rtcTick;
static bool hasRtc;
static InkRomRead romRead;
static void* romContext;
static uint8_t* bankBuffers[2];
static unsigned bankNumbers[2];
static size_t romBytes;
static uint32_t romFingerprint;

static uint32_t stateCrc(uint32_t crc, const void* data, size_t size) {
    static const uint32_t lut[16] = {0,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
        0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const uint8_t* p = data;
    while (size--) { crc ^= *p++; crc = (crc >> 4) ^ lut[crc & 15]; crc = (crc >> 4) ^ lut[crc & 15]; }
    return crc;
}

bool inkCrankIsPaged(void) { return romRead != NULL; }
uint8_t* inkCrankRomBank(uint8_t* rom, unsigned bank, unsigned slot) {
    if (!romRead) return rom + bank * 16384U;
    if (slot > 1 || (size_t)bank * 16384U >= romBytes) {
        snprintf(errorText, sizeof(errorText), "Invalid ROM bank"); longjmp(failure, 1);
    }
    if (bankNumbers[slot] != bank) {
        if (!romRead(romContext, bank * 16384U, bankBuffers[slot], 16384U)) {
            snprintf(errorText, sizeof(errorText), "SD ROM bank read failed"); longjmp(failure, 1);
        }
        bankNumbers[slot] = bank;
    }
    return bankBuffers[slot];
}

void* inkCrankAlloc(size_t bytes) {
    void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) { snprintf(errorText, sizeof(errorText), "Not enough CrankBoy memory"); longjmp(failure, 1); }
    return p;
}
static void fail(gb_s* gb, enum gb_error_e code, uint16_t address) {
    // Match CrankBoy's frontend policy: unused I/O reads return open bus and
    // writes are ignored by the core. Many games probe these during startup.
    if (code == GB_INVALID_READ || code == GB_INVALID_WRITE) return;
    if (code == GB_INVALID_OPCODE)
        snprintf(errorText, sizeof(errorText), "Invalid opcode %02X at PC %04X", address, (uint16_t)(gb->cpu_reg.pc-1));
    else snprintf(errorText, sizeof(errorText), "CrankBoy error %d at %04X", (int)code, address);
    longjmp(failure, 1); // Only unwinds this C adapter, never C++ objects.
}
void __gb_on_breakpoint(gb_s* gb, int number) { fail(gb, GB_INVALID_OPCODE, gb->cpu_reg.pc); }
void __gb_dump_vram(gb_s* gb) { (void)gb; }

void inkGameClose(void) {
    if (core) { free(core->cgb_bg_palette); free(core->cgb_obj_palette); }
    free(core); core = NULL;
    free(workRam); workRam = NULL;
    free(videoRam); videoRam = NULL;
    free(lcd); lcd = NULL;
    free(cartRam); cartRam = NULL;
    free(bankBuffers[0]); free(bankBuffers[1]);
    bankBuffers[0] = bankBuffers[1] = NULL;
    romRead = NULL; romContext = NULL; romBytes = 0;
    ramSize = 0; hasRtc = false;
}
bool inkGameOpen(const uint8_t* rom, size_t bytes) {
    return inkGameOpenBanked(rom, bytes, NULL, NULL);
}
bool inkGameOpenBanked(const uint8_t* rom, size_t bytes, InkRomRead read, void* context) {
    inkGameClose();
    errorText[0] = 0;
    const char* invalid = inkRomValidate(rom, bytes);
    if (invalid) { snprintf(errorText, sizeof(errorText), "%s", invalid); return false; }
    if (setjmp(failure)) { inkGameClose(); return false; }
    romRead = read; romContext = context; romBytes = bytes;
    if (read) {
        bankBuffers[0] = inkCrankAlloc(16384); bankBuffers[1] = inkCrankAlloc(16384);
        bankNumbers[0] = bankNumbers[1] = UINT32_MAX;
    }
    // Identify the actual ROM, not just its title; streamed ROMs use existing
    // bank scratch memory before gb_init binds those windows.
    romFingerprint = UINT32_MAX;
    if (read) {
        for (size_t offset = 0; offset < bytes; offset += 16384) {
            if (!read(context, offset, bankBuffers[0], 16384)) {
                snprintf(errorText, sizeof(errorText), "Cannot read ROM for save identity");
                inkGameClose(); return false;
            }
            romFingerprint = stateCrc(romFingerprint, bankBuffers[0], 16384);
#ifdef ESP_PLATFORM
            if (!(offset & 0x3ffff)) vTaskDelay(1); // Let idle/watchdog run during an 8 MB SD scan.
#endif
        }
    } else romFingerprint = stateCrc(romFingerprint, rom, bytes);
    romFingerprint ^= UINT32_MAX;
    core = inkCrankAlloc(sizeof(*core));
    workRam = inkCrankAlloc(WRAM_SIZE_CGB);
    videoRam = inkCrankAlloc(VRAM_SIZE_CGB);
    lcd = inkCrankAlloc(LCD_BUFFER_BYTES);
    int result = gb_init(core, workRam, videoRam, lcd, (uint8_t*)rom, bytes, fail, NULL, true);
    if (result != GB_INIT_NO_ERROR && result != GB_INIT_NO_ERROR_BUT_REQUIRES_CGB) {
        snprintf(errorText, sizeof(errorText), "Unsupported cartridge (CrankBoy %d)", result);
        inkGameClose(); return false;
    }
    ramSize = gb_get_save_size(core);
    if (ramSize) {
        cartRam = heap_caps_calloc(1, ramSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!cartRam) { snprintf(errorText, sizeof(errorText), "Not enough save memory"); inkGameClose(); return false; }
        if (core->mbc >= 7) memset(cartRam, 0xff, ramSize);
    }
    core->gb_cart_ram = cartRam; core->gb_cart_ram_size = ramSize;
    gb_reset(core, core->is_cgb_mode);
    gb_init_lcd(core);
    core->direct.sound = 1; // APU register/sequencer emulation; no audio output.
    core->direct.frame_skip = 0;
    core->hle_enabled = false;
    hasRtc = rom[0x147] == 0x0f || rom[0x147] == 0x10 || rom[0x147] == 0xfe;
    rtcTick = esp_timer_get_time();
    return true;
}
static void tickRtc(void) {
    if (!core || !hasRtc) return;
    int64_t now = esp_timer_get_time();
    unsigned seconds = (now - rtcTick) / 1000000;
    if (seconds) {
        gb_catch_up_rtc_direct(core, seconds);
        rtcTick += (int64_t)seconds * 1000000;
    }
}
bool inkGameFrame(uint8_t buttons, uint8_t* shades) {
    if (!core || !shades || setjmp(failure)) return false;
    core->direct.joypad = ~buttons;
    tickRtc();
    if (core->is_cgb_mode) gb_run_frame__cgb(core);
    else gb_run_frame__dmg(core);
    return inkGameImage(shades);
}
bool inkGameImage(uint8_t* shades) {
    if (!core || !shades) return false;
    // Packed little-endian, four 2-bit shades per byte; 0=white..3=black.
    for (size_t i = 0; i < LCD_BUFFER_BYTES; ++i) {
        uint8_t p = lcd[i];
        shades[4*i] = p & 3; shades[4*i+1] = (p >> 2) & 3;
        shades[4*i+2] = (p >> 4) & 3; shades[4*i+3] = p >> 6;
    }
    return true;
}

// Deliberately versioned separately from Playdate save files. The native v6
// payload contains pointers, which are never trusted on load; upstream restores
// live allocations and we rebuild every bank/map pointer before execution.
typedef struct {
    char magic[8];
    uint32_t format, bytes, romCrc, payloadCrc, romSize, reserved;
} InkStateHeader;
size_t inkGameStateSize(void) {
    return core ? sizeof(InkStateHeader) + gb_get_state_size_v6(core) + LCD_BUFFER_BYTES : 0;
}
static bool stateError(const char* message) {
    snprintf(errorText, sizeof(errorText), "%s", message); return false;
}
bool inkGameStateExport(void* state, size_t size) {
    if (!core || !state || size != inkGameStateSize()) return stateError("Invalid state buffer");
    tickRtc();
    memset(state, 0, size);
    InkStateHeader* outer = state;
    memcpy(outer->magic, "INKSTATE", 8); outer->format = 1; outer->bytes = size;
    outer->romCrc = romFingerprint; outer->romSize = romBytes;
    StateHeader* header = (void*)(outer + 1);
    memcpy(header->magic, "InkDeck6", 8);
    header->version = 6; header->bits = sizeof(void*); header->cgb = core->is_cgb_mode;
    header->gb_s_size = sizeof(*core);
    time_t now = time(NULL); header->timestamp = now >= 1577836800 ? now : 0;
    gb_state_save_v6(core, (char*)(header + 1));
    memcpy((uint8_t*)state + size - LCD_BUFFER_BYTES, lcd, LCD_BUFFER_BYTES);
    outer->payloadCrc = stateCrc(UINT32_MAX, header, size - sizeof(*outer)) ^ UINT32_MAX;
    return true;
}
bool inkGameStateImport(const void* state, size_t size) {
    if (!core || !state || size != inkGameStateSize()) return stateError("State size/version mismatch");
    const InkStateHeader* outer = state;
    const StateHeader* header = (const void*)(outer + 1);
    const gb_s* incoming = (const void*)(header + 1);
    if (memcmp(outer->magic, "INKSTATE", 8) || outer->format != 1 || outer->bytes != size ||
        memcmp(header->magic, "InkDeck6", 8) || header->version != 6 ||
        header->gb_s_size != sizeof(*core) || header->bits != sizeof(void*) || header->big_endian || header->script)
        return stateError("Incompatible save state");
    if (outer->romCrc != romFingerprint || outer->romSize != romBytes)
        return stateError("Save belongs to a different ROM");
    if ((stateCrc(UINT32_MAX, header, size-sizeof(*outer)) ^ UINT32_MAX) != outer->payloadCrc)
        return stateError("Save state checksum failed");
    if (incoming->gb_cart_ram_size != ramSize || incoming->gb_rom_size != core->gb_rom_size ||
        incoming->mbc != core->mbc || incoming->num_rom_banks_mask != core->num_rom_banks_mask ||
        incoming->num_ram_banks != core->num_ram_banks || incoming->is_cgb_mode != core->is_cgb_mode ||
        incoming->is_mbc1m != core->is_mbc1m || incoming->hle_enabled || incoming->gb_hle ||
        incoming->zero_bank_base >= romBytes || incoming->zero_bank_base % 16384)
        return stateError("Invalid save state metadata");
    const uint8_t* savedRomHeader = (const uint8_t*)(incoming + 1) + 128;
    if (memcmp(savedRomHeader, core->gb_rom + ROM_HEADER_START, ROM_HEADER_SIZE))
        return stateError("Save ROM header mismatch");

    // Read both bank windows BEFORE mutating the live core. An SD read error
    // must leave the current game playable, including its original ROM cache.
    uint8_t* staged = NULL;
    unsigned banks[2] = {incoming->zero_bank_base / 16384, incoming->selected_rom_bank};
    if (incoming->mbc == 1 && !(banks[1] & 31)) ++banks[1];
    banks[0] &= core->num_rom_banks_mask; banks[1] &= core->num_rom_banks_mask;
    if (romRead) {
        staged = heap_caps_calloc(1, 32768, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!staged) return stateError("Not enough state-load memory");
        if (!romRead(romContext, banks[0]*16384U, staged, 16384) ||
            !romRead(romContext, banks[1]*16384U, staged+16384, 16384)) {
            free(staged); return stateError("SD read failed; game unchanged");
        }
    }
    const char* error = gb_state_load_v6(core, (const char*)header, gb_get_state_size_v6(core));
    if (error) { free(staged); return stateError(error); }
    if (staged) {
        for (unsigned slot = 0; slot < 2; ++slot) {
            memcpy(bankBuffers[slot], staged + slot*16384, 16384); bankNumbers[slot] = banks[slot];
        }
        free(staged);
    }
    __gb_init_memory_pointers(core);
    __gb_update_selected_bank_addr(core);
    __gb_update_selected_cart_bank_addr(core);
    __gb_update_zero_bank_addr(core);
    __gb_update_map_pointers(core);
    core->audio.audio_mem = NULL; // Legacy unused field; never retain a serialized address.
    audio_reset_replay_state(&core->audio);
    core->direct.frame_skip = 0; core->direct.joypad = 0xff;
    core->direct.sram_dirty = core->direct.sram_updated = ramSize != 0;
    pgb_cgb_lut_dirty = true;
    memcpy(lcd, (const uint8_t*)state + size - LCD_BUFFER_BYTES, LCD_BUFFER_BYTES);
    int64_t now = time(NULL);
    if (hasRtc && header->timestamp >= 1577836800 && now > header->timestamp && now-header->timestamp <= UINT32_MAX)
        gb_catch_up_rtc_direct(core, (unsigned)(now-header->timestamp));
    rtcTick = esp_timer_get_time(); errorText[0] = 0;
    return true;
}
bool inkGameRestart(void) {
    if (!core || setjmp(failure)) return false;
    tickRtc();
    gb_reset(core, core->is_cgb_mode); gb_init_lcd(core);
    core->direct.sound = 1; core->direct.frame_skip = 0; core->hle_enabled = false;
    core->direct.sram_dirty = core->direct.sram_updated = ramSize != 0;
    rtcTick = esp_timer_get_time(); errorText[0] = 0;
    return true;
}
uint8_t* inkGameRam(size_t* size) { *size = ramSize; return cartRam; }
bool inkGameDirty(void) { return core && (core->direct.sram_dirty || core->direct.sram_updated); }
void inkGameSaved(void) { if (core) core->direct.sram_dirty = core->direct.sram_updated = 0; }
bool inkGameIsColor(void) { return core && core->is_cgb_mode; }
const char* inkGameError(void) { return errorText; }

// Separate versioned sidecar leaves existing raw SRAM saves compatible.
bool inkGameHasRtc(void) { return hasRtc; }
bool inkGameRtcExport(uint8_t state[INK_RTC_STATE_BYTES]) {
    if (!hasRtc || !core) return false;
    tickRtc(); memset(state, 0, INK_RTC_STATE_BYTES); memcpy(state, "IDRT", 4); state[4] = 1;
    state[5] = core->mbc;
    if (core->mbc == 9) { memcpy(state+8, core->huc3.mem, 128); state[136] = core->huc3.sub_seconds; }
    else memcpy(state+6, core->cart_rtc, 5);
    int64_t stamp = time(NULL); if (stamp < 1577836800) stamp = 0;
    memcpy(state+144, &stamp, 8);
    return true;
}
bool inkGameRtcImport(const uint8_t state[INK_RTC_STATE_BYTES]) {
    if (!hasRtc || !core || memcmp(state, "IDRT", 4) || state[4] != 1 || state[5] != core->mbc) return false;
    if (core->mbc == 9) { memcpy(core->huc3.mem, state+8, 128); core->huc3.sub_seconds = state[136]; }
    else { memcpy(core->cart_rtc, state+6, 5); memcpy(core->latched_rtc, core->cart_rtc, 5); }
    int64_t stamp, now = time(NULL); memcpy(&stamp, state+144, 8);
    if (stamp >= 1577836800 && now > stamp && now-stamp <= UINT32_MAX) gb_catch_up_rtc_direct(core, (unsigned)(now-stamp));
    rtcTick = esp_timer_get_time();
    return true;
}
#endif
