#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <InkGenesisPort.h>
#include "../../lib/Apps/AppPaperboy/GenesisCore.h"

// Original, tiny 68000 fixture: enable VDP, set a white backdrop, count in RAM.
static void fixture(uint8_t *rom) {
    memset(rom,0,65536);rom[1]=0xff;rom[2]=0xff;rom[6]=2;
    memcpy(rom+0x100,"SEGA GENESIS    ",16);rom[0x1f0]='U';
    const uint8_t code[]={0x46,0xfc,0x27,0,
        0x33,0xfc,0x81,0x44,0,0xc0,0,4,
        0x23,0xfc,0xc0,0,0,0,0,0xc0,0,4,
        0x33,0xfc,0x0e,0xee,0,0xc0,0,0,
        0x52,0x39,0,0xff,0,0,0x60,0xf8};
    memcpy(rom+0x200,code,sizeof(code));
}
static void testSilentTimers(void) {
    for(unsigned csm=0;csm<2;++csm) {
        FM full={0},silent;FM_Initialise(&full);
        full.state.channel_3_metadata.csm_mode_enabled=csm;
        full.state.timers[0]=(FM_Timer){7,3,cc_true};
        full.state.timers[1]=(FM_Timer){13,5,cc_true};silent=full;
        cc_s16l samples[2*256]={0};FM_OutputSamples(&full,samples,256);
        inkGenesisSilentFm(&silent,256);
        assert(!memcmp(full.state.timers,silent.state.timers,sizeof(full.state.timers)));
        assert(full.state.status==silent.state.status);
    }
}
static void testVideo(ClownMDEmu *core,uint8_t *frame) {
    for(unsigned i=0;i<4;++i) core->callbacks->colour_updated(NULL,i,i*0x555);
    const unsigned widths[]={256,320,512,640},heights[]={224,240,448,480};
    for(unsigned w=0;w<4;++w)for(unsigned h=0;h<4;++h) {
        cc_u8l row[640];unsigned width=widths[w],height=heights[h];
        for(unsigned line=0;line<height;++line) {
            for(unsigned x=0;x<width;++x) row[x]=(x/11+line/7)%4;
            // VDP callbacks can split a line at the window-plane boundary.
            core->callbacks->scanline_rendered(NULL,line,row,0,93,width,height);
            core->callbacks->scanline_rendered(NULL,line,row,93,width,width,height);
        }
        assert(inkGenesisImage(frame));
        for(unsigned y=0;y<120;++y)for(unsigned x=0;x<160;++x)
            assert(frame[(y+12)*160+x]==3-((x*width/160)/11+(y*height/120)/7)%4);
    }
    assert(inkGenesisRestart());assert(inkGenesisFrame(0,frame));
}
void testGenesis(void) {
    uint8_t *rom=malloc(65536),*original=malloc(65536),frame[160*144];
    assert(rom&&original);fixture(rom);memcpy(original,rom,65536);
    assert(inkGenesisOpenGame(rom,65536));assert(inkGenesisPeriod()==16683);
    testSilentTimers();
    for(unsigned i=0;i<3;++i) assert(inkGenesisFrame(0,frame));
    for(unsigned i=0;i<sizeof(frame);++i) assert(frame[i]<=3);
    assert(frame[12*160]==0); // White backdrop.
    ClownMDEmu *core=inkGenesisTestCore();assert(core->state.m68k.ram[0]!=0);
    testVideo(core,frame);
    // ClownZ80 must run even with audio sample generation disabled.
    const uint8_t z80[]={0x3e,0xa5,0x32,0,0x10,0x18,0xfe};
    memcpy(core->state.z80.ram,z80,sizeof(z80));core->state.z80.reset_held=cc_false;
    assert(inkGenesisFrame(0,frame));assert(core->state.z80.ram[0x1000]==0xa5);
    // Input callbacks use the same controls as the previous frontend.
    assert(inkGenesisFrame(1|0x100|0x40,frame));
    assert(core->callbacks->input_requested(NULL,0,CLOWNMDEMU_BUTTON_A));
    assert(core->callbacks->input_requested(NULL,0,CLOWNMDEMU_BUTTON_C));
    assert(core->callbacks->input_requested(NULL,0,CLOWNMDEMU_BUTTON_UP));
    assert(!core->callbacks->input_requested(NULL,0,CLOWNMDEMU_BUTTON_B));
    assert(!core->callbacks->input_requested(NULL,1,CLOWNMDEMU_BUTTON_A));
    size_t size=inkGenesisStateSize();void *saved=malloc(size),*after=malloc(size),*catchup=malloc(size);
    assert(saved&&after&&catchup);assert(inkGenesisStateExport(saved,size));
    uint16_t ram=core->state.m68k.ram[0];core->state.m68k.ram[0]=0;
    assert(inkGenesisStateImport(saved,size));assert(core->state.m68k.ram[0]==ram);
    assert(inkGenesisStateExport(after,size));assert(!memcmp(saved,after,size));
    ((uint8_t*)saved)[size-1]^=1;assert(!inkGenesisStateImport(saved,size));
    assert(core->state.m68k.ram[0]==ram);((uint8_t*)saved)[size-1]^=1;
    for(unsigned i=0;i<6;++i)assert(inkGenesisStep(0,frame,true));
    assert(inkGenesisStateExport(after,size));assert(inkGenesisStateImport(saved,size));
    for(unsigned i=0;i<6;++i)assert(inkGenesisStep(0,frame,i==5));
    assert(inkGenesisStateExport(catchup,size));assert(!memcmp(after,catchup,size));
    inkGenesisCloseGame();assert(!memcmp(rom,original,65536));
    assert(inkGenesisOpenGame(rom,65536));assert(inkGenesisStateImport(saved,size));inkGenesisCloseGame();
    rom[0x150]=1;assert(inkGenesisOpenGame(rom,65536));assert(!inkGenesisStateImport(saved,size));inkGenesisCloseGame();
    fixture(rom);rom[0x1f0]='E';assert(inkGenesisOpenGame(rom,65536));assert(inkGenesisPeriod()==20000);
    assert(inkGenesisFrame(0,frame));inkGenesisCloseGame();
    // Vblank interrupt timing must continue even when draw output is omitted.
    fixture(rom);rom[0x202]=0x20;rom[0x207]=0x64;rom[0x7a]=3;
    const uint8_t irq[]={0x52,0x79,0,0xff,0,2,0x4e,0x73};memcpy(rom+0x300,irq,sizeof(irq));
    assert(inkGenesisOpenGame(rom,65536));
    for(unsigned i=0;i<4;++i) assert(inkGenesisStep(0,NULL,false));
    assert(inkGenesisTestCore()->state.m68k.ram[1]!=0);inkGenesisCloseGame();
    free(catchup);free(after);free(saved);free(original);free(rom);
    puts("ClownMDEmu PASS: CPU/VDP/IRQ, silent FM timers, input, NTSC/PAL, snapshots, ROM restoration and catch-up parity");
}
