// Synthetic code/data only; no commercial ROMs or BIOS images.
#ifdef NDEBUG
#undef NDEBUG // Tests must execute checks even under optimized compiler defaults.
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int allocationBudget = -1;
int64_t testMicros = 0;
#include "../../lib/Apps/AppPaperboy/GameCore.c"

static uint8_t rom[65536], shades[160*144];
static bool bankFailure;
static unsigned bankReads;
static bool readBank(void* context, size_t offset, uint8_t* out, size_t count) {
    (void)context; ++bankReads;
    if (bankFailure) return false;
    if (offset == 0) memcpy(out, rom, count);
    else { memset(out, (offset/16384)&255, count); out[1]=(offset/16384)>>8; }
    return true;
}
static void checksum(void) {
    uint8_t sum=0; for (unsigned i=0x134;i<=0x14c;++i) sum=sum-rom[i]-1;
    rom[0x14d]=sum;
}
static void fixture(uint8_t cgb, uint8_t mapper, uint8_t ram) {
    memset(rom,0,sizeof(rom)); rom[0x143]=cgb; rom[0x147]=mapper; rom[0x149]=ram;
    rom[0x100]=0xc3; rom[0x101]=0x50; rom[0x102]=0x01; // JP 0150
    rom[0x150]=0x18; rom[0x151]=0xfe; // JR -2
    checksum();
}
static void modelTest(uint8_t cgb) {
    fixture(cgb, 0, 0); assert(inkGameOpen(rom,32768));
    assert(inkGameIsColor() == !!(cgb & 0x80));
    assert(core->cpu_reg.a == ((cgb & 0x80) ? 0x11 : 0x01));
    for(int i=0;i<3;++i) assert(inkGameFrame(0,shades));
    for(size_t i=0;i<sizeof(shades);++i) assert(shades[i]<=3);
    assert(inkGameFrame(1,shades)); assert(core->direct.joypad == 0xfe);
    inkGameClose();
}
static void renderTest(uint8_t cgb) {
    fixture(cgb,0,0); assert(inkGameOpen(rom,32768));
    __gb_write_full(core,0xff40,0);
    for(int i=0;i<1024;++i) __gb_write_full(core,0x9800+i,0);
    for(int i=0;i<8;++i) { __gb_write_full(core,0x8000+2*i,0x55); __gb_write_full(core,0x8001+2*i,0x33); }
    if(cgb) {
        unsigned colors[]={0x7fff,20|(20<<5)|(20<<10),10|(10<<5)|(10<<10),0};
        __gb_write_full(core,0xff68,0x80);
        for(int i=0;i<4;++i) { __gb_write_full(core,0xff69,colors[i]); __gb_write_full(core,0xff69,colors[i]>>8); }
    } else __gb_write_full(core,0xff47,0xe4);
    __gb_write_full(core,0xff40,0x91);
    for(int i=0;i<4;++i) assert(inkGameFrame(0,shades));
    for(int y=0;y<144;++y) for(int x=0;x<160;++x) {
        if (shades[y*160+x] != x%4) printf("Render model=%02x x=%d y=%d actual=%d expected=%d\n",cgb,x,y,shades[y*160+x],x%4);
        assert(shades[y*160+x] == x%4);
    }
    // Exercise shifted window/tile/sprite paths with the alignment sanitizer.
    __gb_write_full(core,0xff43,3); __gb_write_full(core,0xff4b,20);
    __gb_write_full(core,0xff4a,9); __gb_write_full(core,0xff40,0xf3);
    for(int i=0;i<4;++i) assert(inkGameFrame(0,shades));
    inkGameClose();
}
static void unusedIoTest(uint8_t cgb) {
    fixture(cgb,0,0);
    const uint8_t program[] = {
        0x3e,0x42,          // LD A,42
        0xe0,0x03,          // LDH (FF03),A: unused register, ignored write
        0xf0,0x03,          // LDH A,(FF03): open bus (FF), not fatal
        0xea,0x00,0xc0,     // LD (C000),A
        0x3e,0x5a,0xea,0x01,0xc0, // Reached the code after both probes
        0x18,0xfe
    };
    memcpy(rom+0x150,program,sizeof(program));
    assert(inkGameOpen(rom,32768));
    for(int i=0;i<3;++i) assert(inkGameFrame(0,shades));
    assert(__gb_read_full(core,0xc000)==0xff);
    assert(__gb_read_full(core,0xc001)==0x5a);
    assert(!*inkGameError());
#ifndef __wasm__
    // Genuine illegal instructions must still stop emulation cleanly.
    rom[0x170]=0xd3; core->cpu_reg.pc=0x170;
    assert(!inkGameFrame(0,shades));
    assert(strstr(inkGameError(),"Invalid opcode D3 at PC 0170"));
#endif
    inkGameClose();
}
static void stateTest(uint8_t cgb, bool banked) {
    fixture(cgb,0x1b,3); rom[0x148]=banked ? 8 : 1; checksum();
    assert(banked ? inkGameOpenBanked(rom,8388608,readBank,NULL) : inkGameOpen(rom,sizeof(rom)));
    for (int i=0;i<3;++i) assert(inkGameFrame(0,shades));
    __gb_write_full(core,0,0x0a); __gb_write_full(core,0xa000,0x67);
    __gb_write_full(core,0xc123,0x93);
    __gb_write_full(core,0x2000,3);
    if (cgb) {
        __gb_write_full(core,0xff70,5); __gb_write_full(core,0xd123,0x77);
        __gb_write_full(core,0xff4f,1);
        core->cgb_bg_palette[47]=0x3a; core->cgb_obj_palette[22]=0x29;
    }
    core->cpu_reg.a=0x42; core->rtc_bits.sec=17; lcd[100]=0xe4;
    uint16_t pc=core->cpu_reg.pc;
    size_t bytes=inkGameStateSize(); assert(bytes>50000 && bytes<1024*1024);
    void* state=malloc(bytes); assert(state && inkGameStateExport(state,bytes));
    core->cpu_reg.a=0x99;
    // Validation failures must leave the live game intact.
    assert(!inkGameStateImport(state,bytes-1)); assert(core->cpu_reg.a==0x99);
    ((uint8_t*)state)[bytes-1]^=1;
    assert(!inkGameStateImport(state,bytes)); assert(core->cpu_reg.a==0x99);
    ((uint8_t*)state)[bytes-1]^=1;
    ((InkStateHeader*)state)->romCrc^=1;
    assert(!inkGameStateImport(state,bytes)); assert(core->cpu_reg.a==0x99);
    ((InkStateHeader*)state)->romCrc^=1;
    ((InkStateHeader*)state)->format=99;
    assert(!inkGameStateImport(state,bytes)); assert(core->cpu_reg.a==0x99);
    ((InkStateHeader*)state)->format=1;
    if (banked) {
        bankFailure=true; assert(!inkGameStateImport(state,bytes)); bankFailure=false;
        assert(core->cpu_reg.a==0x99 && __gb_read_full(core,0x4000)==3);
    }
    inkGameClose();
    assert(banked ? inkGameOpenBanked(rom,8388608,readBank,NULL) : inkGameOpen(rom,sizeof(rom)));
    assert(inkGameStateImport(state,bytes));
    assert(core->cpu_reg.a==0x42 && core->cpu_reg.pc==pc);
    assert(__gb_read_full(core,0xa000)==0x67 && __gb_read_full(core,0xc123)==0x93);
    assert(core->selected_rom_bank==3 && core->rtc_bits.sec==17 && inkGameDirty());
    if (banked) assert(__gb_read_full(core,0x4000)==3);
    if (cgb) {
        assert(core->cgb_vram_bank==1 && core->cgb_wram_bank==5);
        assert(__gb_read_full(core,0xd123)==0x77);
        assert(core->cgb_bg_palette[47]==0x3a && core->cgb_obj_palette[22]==0x29);
    }
    assert(inkGameImage(shades));
    for(unsigned i=0;i<4;++i) assert(shades[400+i]==i);
    assert(inkGameFrame(0,shades));
    assert(inkGameRestart()); assert(inkGameRam(&bytes)[0]==0x67);
    assert(inkGameFrame(0,shades));
    free(state); inkGameClose();
}
int main(void) {
    setbuf(stdout, NULL);
    puts("Models");
    modelTest(0); modelTest(0x80); modelTest(0xc0);
    puts("Unused I/O register startup probes");
    unusedIoTest(0); unusedIoTest(0xc0);
    puts("Rendering");
    renderTest(0); renderTest(0xc0);
    puts("Save states: reload, CPU/RAM/RTC/palettes/LCD, validation, bank read failure");
    stateTest(0,false); stateTest(0xc0,false); stateTest(0xc0,true);
    // Mario-like ROM-only games still get a full resumable snapshot without
    // relying on cartridge battery RAM or an in-game save system.
    fixture(0,0,0); assert(inkGameOpen(rom,32768));
    assert(inkGameFrame(0,shades)); core->cpu_reg.a=0x71;
    size_t noRamSize=inkGameStateSize(); void* noRamState=malloc(noRamSize);
    assert(inkGameStateExport(noRamState,noRamSize)); inkGameClose();
    assert(inkGameOpen(rom,32768) && inkGameStateImport(noRamState,noRamSize));
    assert(core->cpu_reg.a==0x71 && !ramSize); free(noRamState); inkGameClose();
    puts("Validation, banks, DMA, speed switch");
    assert(inkRomFilename("My game.GBC")); assert(inkRomFilename("game.gb"));
    const char* bad[]={"../a.gb","a/b.gbc","a\\b.gb",".gb","a.g","a.gb.exe","a:gb.gbc","a.gbc ","a\n.gb"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!inkRomFilename(bad[i]));
    fixture(0xc0,0x1b,3); rom[0x148]=1; checksum();
    assert(inkGameOpen(rom,sizeof(rom)));
    __gb_write_full(core,0xff40,0); // LCD off for unrestricted VRAM tests.
    __gb_write_full(core,0xff4f,0); __gb_write_full(core,0x8000,0x12);
    __gb_write_full(core,0xff4f,1); __gb_write_full(core,0x8000,0x34);
    assert(__gb_read_full(core,0x8000)==0x34);
    __gb_write_full(core,0xff4f,0); assert(__gb_read_full(core,0x8000)==0x12);
    __gb_write_full(core,0xff70,2); __gb_write_full(core,0xd000,0x42);
    __gb_write_full(core,0xff70,3); __gb_write_full(core,0xd000,0x73);
    __gb_write_full(core,0xff70,2); assert(__gb_read_full(core,0xd000)==0x42);
    // CGB DMA copies WRAM into VRAM.
    for(int i=0;i<16;++i) __gb_write_full(core,0xc000+i,0xa0+i);
    __gb_write_full(core,0xff51,0xc0); __gb_write_full(core,0xff52,0);
    __gb_write_full(core,0xff53,0); __gb_write_full(core,0xff54,0x10);
    __gb_write_full(core,0xff55,0);
    for(int i=0;i<16;++i) assert(__gb_read_full(core,0x8010+i)==0xa0+i);
    // MBC5 bank and SRAM persistence interface.
    rom[0x8000]=0x5a; __gb_write_full(core,0x2000,2);
    assert(__gb_read_full(core,0x4000)==0x5a);
    __gb_write_full(core,0x0000,0x0a); __gb_write_full(core,0xa000,0x6b);
    size_t bytes; assert(inkGameRam(&bytes)[0]==0x6b && bytes==32768 && inkGameDirty());
    inkGameSaved(); assert(!inkGameDirty());
    // CGB speed-switch STOP must activate double-speed, not DMG fallback.
    core->cpu_reg.pc=0x160; rom[0x160]=0x10; rom[0x161]=0;
    rom[0x162]=0x18; rom[0x163]=0xfe;
    __gb_write_full(core,0xff4d,1);
    assert(inkGameFrame(0,shades)); assert(core->cgb_fast_mode);
    inkGameClose();
    // Cartridge RTC survives a sidecar round-trip and advances while running.
    puts("RTC");
    fixture(0,0x0f,0); assert(inkGameOpen(rom,32768));
    __gb_write_full(core,0,0x0a);
    assert(__gb_read_full(core,0xa000)==0xff); __gb_write_full(core,0xbfff,0x55);
    inkGameClose();
    fixture(0,0x03,1); assert(inkGameOpen(rom,32768));
    __gb_write_full(core,0,0x0a); __gb_write_full(core,0xbfff,0x42);
    assert(__gb_read_full(core,0xa7ff)==0x42 && __gb_read_full(core,0xbfff)==0x42);
    assert(!core->selected_cart_bank_addr); inkGameClose();
    fixture(0xc0,0x10,3); assert(inkGameOpen(rom,32768));
    uint8_t rtc[INK_RTC_STATE_BYTES]; core->rtc_bits.sec=20;
    testMicros += 2000000; assert(inkGameRtcExport(rtc)); assert(rtc[6]==22);
    core->rtc_bits.sec=0; assert(inkGameRtcImport(rtc)); assert(core->rtc_bits.sec>=22);
    rtc[4]=99; assert(!inkGameRtcImport(rtc)); inkGameClose();
    fixture(0xc0,0xfe,3); assert(inkGameOpen(rom,32768));
    for(int i=0;i<128;++i) core->huc3.mem[i]=i;
    assert(inkGameRtcExport(rtc)); memset(core->huc3.mem,0,128);
    assert(inkGameRtcImport(rtc)); for(int i=0;i<128;++i) assert(core->huc3.mem[i]==i);
    inkGameClose();
    // 8 MB MBC5: pin both windows and reach every 9-bit bank without an 8 MB allocation.
    puts("8 MB ROM");
    fixture(0xc0,0x1b,3); rom[0x148]=8; checksum(); bankReads=0;
    assert(inkGameOpenBanked(rom,8388608,readBank,NULL));
    for(unsigned bank=1;bank<512;++bank) {
        __gb_write_full(core,0x2000,bank&255); __gb_write_full(core,0x3000,bank>>8);
        assert(__gb_read_full(core,0x4000)==(bank&255));
        assert(__gb_read_full(core,0x4001)==(bank>>8));
        assert(__gb_read_full(core,0x100)==0xc3);
    }
    unsigned previousReads=bankReads;
    for(int i=0;i<100;++i) assert(__gb_read_full(core,0x4000)==255);
    assert(previousReads==bankReads); inkGameClose();
    // A failed bank read must unwind the frame cleanly, not execute partial data.
#ifndef __wasm__
    rom[0x150]=0x3e; rom[0x151]=2; rom[0x152]=0xea; rom[0x153]=0; rom[0x154]=0x20;
    assert(inkGameOpenBanked(rom,8388608,readBank,NULL)); bankFailure=true;
    assert(!inkGameFrame(0,shades)); assert(strstr(inkGameError(),"bank read failed"));
    bankFailure=false; inkGameClose();
#endif
    // Bad input and allocation failures unwind without running or saving.
    puts("Invalid input");
    fixture(0xc0,0,0); rom[0x149]=6; checksum(); assert(!inkGameOpen(rom,32768));
    fixture(0,0,0); rom[0x14d]^=1; assert(!inkGameOpen(rom,32768));
    fixture(0,0,0); assert(!inkGameOpen(rom,32767)); assert(!inkGameOpen(rom,32769));
#ifndef __wasm__
    for(int i=0;i<6;++i) { allocationBudget=i; assert(!inkGameOpen(rom,32768)); inkGameClose(); }
#endif
    allocationBudget=-1; modelTest(0xc0);
    puts("CrankBoy tests PASS: DMG/CGB, four shades, banks (including 8MB), DMA, speed switch, input, SRAM, RTC, validation");
#ifdef __wasm__
    puts("WASI run excludes setjmp/longjmp fault-injection cases (native suite only)");
#else
    puts("Native allocation and bank-read fault-injection tests PASS");
#endif
}
