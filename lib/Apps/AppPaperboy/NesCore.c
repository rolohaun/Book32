#if defined(BOARD_LILYGO_T5S3_PRO)
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nofrendo.h>
#include <nes/state.h>
#include "NesCore.h"
#include "RomFormat.h"
#include "ConsoleState.h"
#include <InkNesStateIO.h>
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif
#undef FILE
#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef fclose
InkNesStream inkNesStream;
static nes_t* machine;
static uint8_t *video,*image,*nativeImage;
static uint32_t fingerprint;
static size_t romBytes;
static uint8_t palette[256];
static bool thumbnailDirty;
static bool nativeDirty,indexedValid;
static const char* error="";
enum { STATE_CAPACITY=65536, NATIVE_BYTES=256*240, PREVIEW_BYTES=NATIVE_BYTES/4,
       PREVIEW_OFFSET=STATE_CAPACITY-PREVIEW_BYTES-4 };
static void* frameAlloc(size_t bytes) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(bytes,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(bytes,1);
#endif
}
void inkNesCloseGame(void) {
    if (machine) nes_shutdown(); machine=NULL;
    free(video);free(image);free(nativeImage);video=image=nativeImage=NULL;nativeDirty=indexedValid=false;
}
bool inkNesOpenGame(uint8_t* rom,size_t bytes) {
    inkNesCloseGame(); error=inkRomValidateSystem(INK_SYSTEM_NES,rom,bytes);
    if(error) return false;
    error="Not enough NES memory";
    video=frameAlloc(NES_SCREEN_PITCH*NES_SCREEN_HEIGHT);
    image=frameAlloc(INK_CONSOLE_IMAGE_BYTES);
    nativeImage=frameAlloc(NATIVE_BYTES);
    machine=nes_init(SYS_DETECT,8000,false,NULL);
    if (!video || !image || !nativeImage || !machine) { inkNesCloseGame(); return false; }
    rom_t* cart=rom_loadmem(rom,bytes);
    if (!cart) { inkNesCloseGame(); return false; }
    if(cart->system==SYS_UNKNOWN) cart->system=(rom[9]&1) ? SYS_NES_PAL : SYS_NES_NTSC;
    if (nes_insertcart(cart)<0) { error="Unsupported NES mapper"; inkNesCloseGame(); return false; }
    nes_setvidbuf(video); input_connect(0,NES_JOYPAD); input_connect(1,NES_NOTHING);
    uint16_t* rgb=nofrendo_buildpalette(NES_PALETTE_NESCLASSIC,16);
    if (!rgb) { inkNesCloseGame(); return false; }
    for(unsigned i=0;i<256;i++) palette[i]=inkGray565(rgb[i]);
    free(rgb);fingerprint=inkConsoleCrc(rom,bytes);romBytes=bytes;thumbnailDirty=false;error="";return true;
}
static void makeNative(void) {
    if(!nativeDirty) return;
    uint8_t indexed[256],converted[256];
    for(unsigned y=0;y<240;y++) {
        memcpy(indexed,video+y*NES_SCREEN_PITCH+NES_SCREEN_OVERDRAW,sizeof(indexed));
        for(unsigned x=0;x<256;x++)converted[x]=palette[indexed[x]];
        memcpy(nativeImage+y*256,converted,sizeof(converted));
    }
    nativeDirty=false;
}
static void makeThumbnail(void) {
    if(!thumbnailDirty) return;
    makeNative();
    memset(image,0,INK_CONSOLE_IMAGE_BYTES);
    uint8_t row[256],small[160];
    for(unsigned y=0;y<120;y++) {
        memcpy(row,nativeImage+y*2*256,sizeof(row));
        for(unsigned x=0;x<160;x++)small[x]=row[x*256/160];
        memcpy(image+(y+12)*160,small,sizeof(small));
    }
    thumbnailDirty=false;
}
bool inkNesFrame(uint16_t buttons,uint8_t* shades) {
    if (!machine) return false;
#ifdef ESP_PLATFORM
    const int64_t started=esp_timer_get_time();
#endif
    // Ink Boy keeps GB's established direction bits; NES pad ordering differs.
    uint8_t pad=(buttons&15)|((buttons&0x10)<<3)|((buttons&0x20)<<1)|((buttons&0x40)>>2)|((buttons&0x80)>>2);
    input_update(0,pad);nes_emulate(true);
#ifdef ESP_PLATFORM
    const int64_t emulated=esp_timer_get_time();
#endif
    indexedValid=nativeDirty=true;
    // Keep the original thumbnail ABI / save envelope for compatibility only.
    // This thumbnail is never used for normal NES gameplay display.
    thumbnailDirty=true;
    if(shades) { makeThumbnail();memcpy(shades,image,INK_CONSOLE_IMAGE_BYTES); }
#ifdef ESP_PLATFORM
    static uint64_t coreUs,convertUs;static unsigned frames;
    coreUs+=emulated-started;convertUs+=esp_timer_get_time()-emulated;
    if(++frames==300) {
        printf("NES stages: CPU/PPU %.2f ms; conversion %.2f ms\n",coreUs/(1000.0*frames),convertUs/(1000.0*frames));
        frames=0;coreUs=convertUs=0;
    }
#endif
    return true;
}
bool inkNesImage(uint8_t* out) { if(!image||!out)return false;makeThumbnail();memcpy(out,image,INK_CONSOLE_IMAGE_BYTES);return true; }
const uint8_t* inkNesNativeImage(void) { makeNative();return nativeImage; }
const uint8_t* inkNesIndexedImage(unsigned* pitch,const uint8_t** shades) {
    if(pitch)*pitch=NES_SCREEN_PITCH;if(shades)*shades=palette;
    return indexedValid ? video+NES_SCREEN_OVERDRAW : NULL;
}
bool inkNesRestart(void) { if(!machine)return false;nes_reset(false);nes_setvidbuf(video);input_connect(0,NES_JOYPAD);memset(image,0,INK_CONSOLE_IMAGE_BYTES);memset(nativeImage,0,NATIVE_BYTES);thumbnailDirty=nativeDirty=indexedValid=false;return true; }
uint8_t* inkNesRam(size_t* bytes) { *bytes=machine&&machine->cart->battery ? machine->cart->prg_ram_banks*ROM_PRG_BANK_SIZE : 0;return *bytes ? machine->cart->prg_ram : NULL; }
unsigned inkNesPeriod(void) { return machine&&machine->refresh_rate==50 ? 20000 : 16667; }
const char* inkNesError(void) { return error ? error : "NES load failed"; }
size_t inkNesStateSize(void) { return machine ? sizeof(InkConsoleHeader)+INK_CONSOLE_IMAGE_BYTES+STATE_CAPACITY : 0; }
bool inkNesStateExport(void* data,size_t size) {
    if(!data||size!=inkNesStateSize())return false;
    makeThumbnail();
    memset(data,0,size);uint8_t* p=(uint8_t*)data+sizeof(InkConsoleHeader);
    memcpy(p,image,INK_CONSOLE_IMAGE_BYTES);
    inkNesStream=(InkNesStream){p+INK_CONSOLE_IMAGE_BYTES,STATE_CAPACITY,0,0,0};
    if(state_save("memory") || inkNesStream.failed)return false;
    // Store a lossless 2-bit native preview in unused envelope capacity. Older
    // builds ignore this tail; old saves (all-zero tail) still load unchanged.
    if(inkNesStream.length<=PREVIEW_OFFSET) {
        uint8_t* preview=p+INK_CONSOLE_IMAGE_BYTES+PREVIEW_OFFSET;
        memcpy(preview,"NES2",4);
        for(unsigned i=0;i<PREVIEW_BYTES;i++) preview[4+i]=
            nativeImage[i*4]|(nativeImage[i*4+1]<<2)|(nativeImage[i*4+2]<<4)|(nativeImage[i*4+3]<<6);
    }
    inkConsoleSeal(data,size,INK_SYSTEM_NES,fingerprint,romBytes,inkNesStream.length);return true;
}
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static bool validState(const uint8_t* p,size_t size) {
    if(size<8||memcmp(p,"SNSS",4))return false;
    uint32_t blocks=be32(p+4),mask=0;size_t off=8;
    if(blocks<5||blocks>6)return false;
    for(unsigned i=0;i<blocks;i++) {
        if(off>size||size-off<12)return false;
        uint32_t len=be32(p+off+8),bit=0,expected=0;
        if(be32(p+off+4)!=1||len>size-off-12)return false;
        const uint8_t* b=p+off+12;
        if(!memcmp(p+off,"BASR",4)) { bit=1;expected=6449;if(len!=expected)return false;for(int n=0;n<4;n++)if(b[6441+n]>3)return false; }
        else if(!memcmp(p+off,"INFO",4)){bit=2;expected=256;}
        else if(!memcmp(p+off,"SOUN",4)){bit=4;expected=22;}
        else if(!memcmp(p+off,"VRAM",4)){bit=8;expected=8192;}
        else if(!memcmp(p+off,"SRAM",4)){bit=16;expected=8193;}
        else if(!memcmp(p+off,"MPRD",4)){bit=32;expected=536;}
        if(!bit||len!=expected||(mask&bit))return false;
        mask|=bit;off+=12+len;
    }
    return off==size&&mask==(machine->mapper->number ? 63U : 31U);
}
bool inkNesStateImport(const void* data,size_t size) {
    error="Invalid, incompatible or wrong-ROM NES save";
    if(!machine||!inkConsoleCheck(data,size,INK_SYSTEM_NES,fingerprint,romBytes,STATE_CAPACITY))return false;
    InkConsoleHeader h;memcpy(&h,data,sizeof(h));
    const uint8_t* p=(const uint8_t*)data+sizeof(h);
    if(!validState(p+INK_CONSOLE_IMAGE_BYTES,h.used))return false;
    inkNesStream=(InkNesStream){(uint8_t*)p+INK_CONSOLE_IMAGE_BYTES,STATE_CAPACITY,0,h.used,0};
    if(state_load("memory")||inkNesStream.failed)return false;
    memcpy(image,p,INK_CONSOLE_IMAGE_BYTES);thumbnailDirty=nativeDirty=indexedValid=false;
    const uint8_t* preview=p+INK_CONSOLE_IMAGE_BYTES+PREVIEW_OFFSET;
    if(h.used<=PREVIEW_OFFSET && !memcmp(preview,"NES2",4)) {
        for(unsigned i=0;i<NATIVE_BYTES;i++) nativeImage[i]=(preview[4+i/4]>>(2*(i&3)))&3;
    } else {
        // Legacy saves contain only a thumbnail. Show it until the next real
        // emulated frame; do not advance the saved CPU just to create a preview.
        for(unsigned y=0;y<240;y++) for(unsigned x=0;x<256;x++)
            nativeImage[y*256+x]=image[(y/2+12)*160+x*160/256]&3;
    }
    nes_setvidbuf(video);machine->scanline=0;machine->cycles=0;error="";return true;
}
#endif
