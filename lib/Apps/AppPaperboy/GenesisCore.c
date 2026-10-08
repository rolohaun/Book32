#if defined(BOARD_LILYGO_T5S3_PRO)
// InkDeck ClownMDEmu frontend. AGPL-3.0-or-later; see THIRD_PARTY_NOTICES.md.
#include <InkGenesisPort.h>
#include "GenesisCore.h"
#include "RomFormat.h"
#include "ConsoleState.h"
#include <stdio.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

static ClownMDEmu* core;
static uint8_t *image, *romBytes;
static size_t romSize;
static uint32_t fingerprint;
static uint16_t buttons;
static uint8_t palette[VDP_TOTAL_COLOURS];
static bool rendering, pal;
static const char* error = "No Genesis game loaded";

typedef struct {
    uint32_t format; // Distinct from the retired Gwenesis state format.
    ClownMDEmu_StateBackup machine;
    ControllerManager_State controllers;
} InkClownState;
#define INK_CLOWN_STATE_FORMAT 0x434c0001U

static void* allocate(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}
static void colour(void* unused, cc_u16f index, cc_u16f rgb) {
    (void)unused;
    if (index < sizeof(palette)) {
        unsigned luminance = (rgb & 15)*77 + ((rgb >> 4)&15)*150 + ((rgb >> 8)&15)*29;
        palette[index] = 3 - (luminance >> 10);
    }
}
static void rebuildPalette(void) {
    for (unsigned i=0; i<64; ++i) {
        unsigned rgb=core->vdp.state.cram[i];
        colour(NULL,i,rgb | ((rgb & 0x888)>>3));
        colour(NULL,i+64,rgb>>1);
        colour(NULL,i+128,0x888+(rgb>>1));
    }
}
static void scanline(void* unused, cc_u16f line, const cc_u8l* pixels,
                     cc_u16f left, cc_u16f right, cc_u16f width, cc_u16f height) {
    (void)unused;
    if (!rendering || !width || !height || left>=right || right>width) return;
    // Keep the existing 160x120 image, letterboxed in a 160x144 Game Boy-sized
    // viewport. Integer mapping handles 256/320-wide and interlaced content.
    unsigned y=(line*120U + height-1)/height;
    if (y>=120 || y*height/120U!=line) return;
    uint8_t* row=image+(y+12)*160;
    for (unsigned x=0;x<160;++x) {
        unsigned sx=x*width/160U;
        if (sx>=left && sx<right) row[x]=palette[pixels[sx] % VDP_TOTAL_COLOURS];
    }
}
static cc_bool input(void* unused, cc_u8f player, ClownMDEmu_Button button) {
    (void)unused;
    static const uint16_t masks[CLOWNMDEMU_BUTTON_MAX]={0x40,0x80,0x20,0x10,1,2,0x100,0,0,0,8,0};
    return player==0 && button<CLOWNMDEMU_BUTTON_MAX && (buttons&masks[button])!=0;
}

// FM_Update advances the BUSY flag itself, but timer A/B expire in the audio
// callback. Preserve that CPU-visible behavior without generating samples.
// This follows ClownMDEmu's FM_OutputSamples timer section (Clownacy, AGPLv3+).
void inkGenesisSilentFm(FM* fm, size_t frames) {
    for (size_t frame=0;frame<frames;++frame) {
        for (unsigned i=0;i<2;++i) {
            FM_Timer* timer=&fm->state.timers[i];
            if (--timer->counter==0) {
                fm->state.status |= timer->enabled ? 1U<<i : 0;
                timer->counter=timer->value;
                if (i==0 && fm->state.channel_3_metadata.csm_mode_enabled)
                    for (unsigned op=0;op<4;++op) {
                        FM_Channel_SetKeyOn(&fm->state.channels[2].state,op,cc_true);
                        FM_Channel_SetKeyOn(&fm->state.channels[2].state,op,cc_false);
                    }
            }
        }
    }
}
static void fmAudio(void* unused, ClownMDEmu* machine, size_t frames,
                    void (*generate)(ClownMDEmu*,cc_s16l*,size_t)) {
    (void)unused;(void)generate;inkGenesisSilentFm(&machine->fm,frames);
}
static void noAudio(void* unused, ClownMDEmu* machine, size_t frames,
                    void (*generate)(ClownMDEmu*,cc_s16l*,size_t)) {
    (void)unused;(void)machine;(void)frames;(void)generate;
}
static cc_bool noSaveRead(void* unused,const char* name) {(void)unused;(void)name;return cc_false;}
static cc_s16f noRead(void* unused) {(void)unused;return -1;}
static void noWrite(void* unused,cc_u8f byte) {(void)unused;(void)byte;}
static void noClose(void* unused) {(void)unused;}
static cc_bool noSize(void* unused,const char* name,size_t* size) {(void)unused;(void)name;*size=0;return cc_false;}
static const ClownMDEmu_Callbacks callbacks={
    .colour_updated=colour,.scanline_rendered=scanline,.input_requested=input,
    .fm_audio_to_be_generated=fmAudio,.psg_audio_to_be_generated=noAudio,
    .pcm_audio_to_be_generated=noAudio,.cdda_audio_to_be_generated=noAudio,
    .save_file_opened_for_reading=noSaveRead,.save_file_read=noRead,
    .save_file_opened_for_writing=noSaveRead,.save_file_written=noWrite,
    .save_file_closed=noClose,.save_file_removed=noSaveRead,.save_file_size_obtained=noSize
};

void inkGenesisCloseGame(void) {
    free(core);core=NULL;free(image);image=NULL;inkGenesisLookupFree();
    // Return the caller's resident buffer to its original big-endian bytes.
    if (romBytes) for (size_t i=0;i<romSize;i+=2) {
        uint16_t word;memcpy(&word,romBytes+i,2);romBytes[i]=word>>8;romBytes[i+1]=word;
    }
    romBytes=NULL;romSize=0;buttons=0;
}
bool inkGenesisOpenGame(uint8_t* rom,size_t bytes) {
    inkGenesisCloseGame();error=inkRomValidateSystem(INK_SYSTEM_SEGA,rom,bytes);
    if (error) return false;
    if ((uintptr_t)rom%2 || bytes%2) {error="Genesis ROM must contain aligned 16-bit words";return false;}
    core=allocate(sizeof(*core));image=allocate(INK_CONSOLE_IMAGE_BYTES);
    if (!core || !image || !inkGenesisLookupAllocate(allocate)) {
        error="Not enough memory for Genesis";inkGenesisCloseGame();return false;
    }
    bool usa=false,japan=false,europe=false;
    for(unsigned i=0x1f0;i<0x200;++i) {
        unsigned c=rom[i];usa|=c=='U'||c=='4';japan|=c=='J'||c=='1';europe|=c=='E'||c=='8';
    }
    pal=!memcmp(rom+0x1f0,"EUROPE",6) || (europe&&!usa&&!japan);
    ClownMDEmu_InitialConfiguration config={0};
    config.general.region=japan&&!usa&&!pal?CLOWNMDEMU_REGION_DOMESTIC:CLOWNMDEMU_REGION_OVERSEAS;
    config.general.tv_standard=pal?CLOWNMDEMU_TV_STANDARD_PAL:CLOWNMDEMU_TV_STANDARD_NTSC;
    config.general.low_pass_filter_disabled=cc_true;
    config.general.cd_add_on_enabled=cc_false;
    fingerprint=inkConsoleCrc(rom,bytes);
    ClownMDEmu_Constant_Initialise();ClownMDEmu_Initialise(core,&config,&callbacks);
    for (size_t i=0;i<bytes;i+=2) {uint16_t word=(rom[i]<<8)|rom[i+1];memcpy(rom+i,&word,2);}
    romBytes=rom;romSize=bytes;
    ClownMDEmu_SetCartridge(core,(const cc_u16l*)rom,bytes/2);
    ClownMDEmu_HardReset(core,cc_true,cc_false);rebuildPalette();error="";
    printf("Genesis: ClownMDEmu; state %u bytes; no audio synthesis\n",(unsigned)sizeof(*core));
    return true;
}
unsigned inkGenesisPeriod(void){return pal?20000:16683;}
const char* inkGenesisError(void){return error?error:"Genesis error";}
bool inkGenesisFrame(uint16_t keys,uint8_t* shades){return inkGenesisStep(keys,shades,true);}
bool inkGenesisStep(uint16_t keys,uint8_t* shades,bool render) {
    if (!core || (render&&!shades)) return false;
    buttons=keys;rendering=render;ClownMDEmu_Iterate(core);
    if (render) memcpy(shades,image,INK_CONSOLE_IMAGE_BYTES);
    return true;
}
bool inkGenesisImage(uint8_t* out){if(!core||!out)return false;memcpy(out,image,INK_CONSOLE_IMAGE_BYTES);return true;}
bool inkGenesisRestart(void){if(!core)return false;ClownMDEmu_HardReset(core,cc_true,cc_false);rebuildPalette();memset(image,0,INK_CONSOLE_IMAGE_BYTES);buttons=0;return true;}
size_t inkGenesisStateSize(void){return core?sizeof(InkConsoleHeader)+INK_CONSOLE_IMAGE_BYTES+sizeof(InkClownState):0;}
bool inkGenesisStateExport(void* data,size_t size) {
    if (!core||!data||size!=inkGenesisStateSize()) return false;
    memset(data,0,size);uint8_t* payload=(uint8_t*)data+sizeof(InkConsoleHeader);
    memcpy(payload,image,INK_CONSOLE_IMAGE_BYTES);
    InkClownState* state=(InkClownState*)(payload+INK_CONSOLE_IMAGE_BYTES);
    state->format=INK_CLOWN_STATE_FORMAT;ClownMDEmu_SaveState(core,&state->machine);
    state->controllers=core->controller_manager.state;
    inkConsoleSeal(data,size,INK_SYSTEM_SEGA,fingerprint,romSize,sizeof(*state));return true;
}
bool inkGenesisStateImport(const void* data,size_t size) {
    error="Invalid or incompatible ClownMDEmu save (old Genesis saves are not compatible)";
    if (!core||!inkConsoleCheck(data,size,INK_SYSTEM_SEGA,fingerprint,romSize,sizeof(InkClownState))) return false;
    const uint8_t* payload=(const uint8_t*)data+sizeof(InkConsoleHeader);
    const InkClownState* state=(const InkClownState*)(payload+INK_CONSOLE_IMAGE_BYTES);
    if (state->format!=INK_CLOWN_STATE_FORMAT || !state->machine.general.cartridge_inserted ||
        state->machine.general.external_ram.size>0x10000 || state->machine.general.mega_cd.cd_inserted) return false;
    ClownMDEmu_LoadState(core,&state->machine);core->controller_manager.state=state->controllers;
    rebuildPalette();memcpy(image,payload,INK_CONSOLE_IMAGE_BYTES);error="";return true;
}
#ifndef ESP_PLATFORM
ClownMDEmu* inkGenesisTestCore(void){return core;}
#endif
#endif
