// Original synthetic 6502 program; no copyrighted ROM assets.
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <nofrendo.h>
#include "../../lib/Apps/AppPaperboy/NesCore.h"
#include "../../lib/Apps/AppPaperboy/RomFormat.h"
#include "../../lib/Apps/AppPaperboy/ConsoleState.h"
void testNesPpuRows(void);
void testNes(void) {
    uint8_t rom[16400]={0},frame[160*144];
    memcpy(rom,"NES\x1a",4);rom[4]=1;rom[6]=2;
    const uint8_t setup[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0,0x8d,0,0x20,0x8d,1,0x20,
        0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,0xa9,0x30,0x8d,7,0x20,0xa9,0x0a,0x8d,1,0x20};
    memcpy(rom+16,setup,sizeof(setup));size_t loop=16+sizeof(setup);
    rom[loop]=0xe6;rom[loop+1]=0;rom[loop+2]=0x4c;rom[loop+3]=sizeof(setup);rom[loop+4]=0x80;
    for(int i=0;i<3;i++){rom[16+0x3ffa+i*2]=0;rom[16+0x3ffb+i*2]=0x80;}
    assert(inkRomValidateSystem(INK_SYSTEM_NES,rom,sizeof(rom))==NULL);
    assert(inkNesOpenGame(rom,sizeof(rom)));assert(inkNesPeriod()==16667);
    testNesPpuRows();
    for(int i=0;i<8;i++)assert(inkNesFrame(0x11,frame));
    nes_t* nes=nes_getptr();assert(nes->input[0].state==(NES_PAD_A|NES_PAD_RIGHT));
    assert(nes->cpu->pc_reg>=0x8000&&nes->cpu->pc_reg<0x8040);
    for(unsigned i=0;i<sizeof(frame);i++)assert(frame[i]<=3);
    const uint8_t* native=inkNesNativeImage();assert(native);
    unsigned pitch;const uint8_t* shades;const uint8_t* indexed=inkNesIndexedImage(&pitch,&shades);
    assert(indexed==nes->vidbuf+NES_SCREEN_OVERDRAW && pitch==NES_SCREEN_PITCH);
    uint16_t* rgb=nofrendo_buildpalette(NES_PALETTE_NESCLASSIC,16);assert(rgb);
    // All 256 columns / 240 lines, including odd rows and both outer edges.
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)
        assert(native[y*256+x]==inkGray565(rgb[nes->vidbuf[y*NES_SCREEN_PITCH+NES_SCREEN_OVERDRAW+x]]));
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)assert(native[y*256+x]==shades[indexed[y*pitch+x]]);
    free(rgb);
    // A nonuniform native preview must survive saving without downsampling.
    uint8_t* nativeSaved=malloc(256*240);assert(nativeSaved);
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)
        nativeSaved[y*256+x]=((uint8_t*)native)[y*256+x]=(x*3+y*7)&3;
    size_t ramBytes;uint8_t* ram=inkNesRam(&ramBytes);assert(ram&&ramBytes==8192);
    ram[3]=0x7a;nes->mem->ram[10]=0x45;
    size_t size=inkNesStateSize();uint8_t* saved=malloc(size);assert(saved&&inkNesStateExport(saved,size));
    ram[3]=0x22;nes->mem->ram[10]=0x99;assert(inkNesStateImport(saved,size));
    assert(!inkNesIndexedImage(&pitch,&shades)); // Never substitute a stale PPU image for a restored preview.
    assert(!memcmp(nativeSaved,inkNesNativeImage(),256*240));
    assert(ram[3]==0x7a&&nes->mem->ram[10]==0x45);
    saved[size-1]^=1;assert(!inkNesStateImport(saved,size));assert(nes->mem->ram[10]==0x45);saved[size-1]^=1;
    inkNesCloseGame();assert(inkNesOpenGame(rom,sizeof(rom)));assert(inkNesStateImport(saved,size));
    assert(nes_getptr()->mem->ram[10]==0x45);assert(inkNesFrame(0,frame));
    // Simulate an inkboy8 save: same envelope and SNSS payload, empty tail.
    uint8_t* legacy=malloc(size);assert(legacy);memcpy(legacy,saved,size);
    memset(legacy+size-256*240/4-4,0,256*240/4+4);
    InkConsoleHeader h;memcpy(&h,legacy,sizeof(h));
    inkConsoleSeal(legacy,size,h.system,h.romCrc,h.romBytes,h.used);
    assert(inkNesStateImport(legacy,size));assert(nes_getptr()->mem->ram[10]==0x45);
    assert(inkNesImage(frame));native=inkNesNativeImage();
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)assert(native[y*256+x]==frame[(y/2+12)*160+x*160/256]);
    free(legacy);free(nativeSaved);
    // Zero-filled RAM must replace nonzero data as well.
    ram=inkNesRam(&ramBytes);memset(ram,0,ramBytes);assert(inkNesStateExport(saved,size));ram[3]=9;
    assert(inkNesStateImport(saved,size)&&ram[3]==0);
    assert(inkNesRestart());assert(inkNesFrame(0,NULL));assert(inkNesImage(frame));
    native=inkNesNativeImage();
    for(int y=0;y<120;y++)for(int x=0;x<160;x++)assert(frame[(y+12)*160+x]==native[y*2*256+x*256/160]);
    inkNesCloseGame();inkNesCloseGame();
    rom[0x110]^=1;assert(inkNesOpenGame(rom,sizeof(rom)));assert(!inkNesStateImport(saved,size));inkNesCloseGame();
    rom[9]=1;assert(inkNesOpenGame(rom,sizeof(rom)));assert(inkNesPeriod()==20000);assert(inkNesFrame(0,frame));inkNesCloseGame();
    free(saved);
    rom[6]=0xf0;rom[7]=0xf0;assert(!inkNesOpenGame(rom,sizeof(rom)));inkNesCloseGame();
    assert(!inkNesNativeImage());
    puts("NES PASS: native 256x240 pixels, lossless previews, legacy saves, execution, input, snapshots, corruption, mappers");
}
