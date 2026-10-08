#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../lib/Apps/AppPaperboy/RomFormat.h"
#include "../../lib/Apps/AppPaperboy/ConsoleCore.h"
int allocationBudget=-1;
int64_t testMicros=0;
void testNes(void);void testRomFormats(void);
static void testDispatch(void){
    uint8_t* rom=calloc(65536,1);uint8_t frame[160*144];assert(rom);
    // Exercise the same dispatch boundary as the device picker, changing cores
    // without restarting the process. GB's existing save-state format remains.
    for(int pass=0;pass<2;pass++) {
        memset(rom,0,65536);rom[0x100]=0x18;rom[0x101]=0xfe;
        uint8_t checksum=0;for(int i=0x134;i<=0x14c;i++)checksum=checksum-rom[i]-1;
        rom[0x14d]=checksum;
        assert(inkConsoleOpen(INK_SYSTEM_GB,rom,32768,NULL,NULL));
        assert(inkConsoleSystem()==INK_SYSTEM_GB&&strstr(inkConsoleName(),"CrankBoy"));
        assert(inkConsolePeriod()==16743&&inkConsoleFrame(0,frame));
        assert(inkConsoleFrame(0,frame));
        size_t size=inkConsoleStateSize();void* saved=malloc(size);assert(saved);
        assert(inkConsoleStateExport(saved,size)&&inkConsoleStateImport(saved,size));free(saved);
        inkConsoleClose();
        memset(rom,0,65536);memcpy(rom,"NES\x1a",4);rom[4]=1;
        rom[16]=0x4c;rom[17]=0;rom[18]=0x80;
        for(int i=0;i<3;i++)rom[16+0x3ffb+2*i]=0x80;
        assert(inkConsoleOpen(INK_SYSTEM_NES,rom,16400,NULL,NULL));
        assert(inkConsoleSystem()==INK_SYSTEM_NES&&strstr(inkConsoleName(),"Nofrendo"));
        assert(inkConsoleFrame(0,frame));assert(inkConsoleFrame(0,frame));inkConsoleClose();
        assert(!inkConsoleOpen((InkSystem)3,rom,65536,NULL,NULL));
        assert(inkConsoleSystem()==INK_SYSTEM_NONE);
    }
    free(rom);assert(inkConsoleSystem()==INK_SYSTEM_NONE);
    puts("Dispatcher PASS: GB/NES switching and unchanged GB snapshots");
}
int main(void){
    assert(inkRomFilename("Game.GBC")&&inkRomSystem("Game.GBC")==INK_SYSTEM_GB);
    assert(inkRomSystem("Game.NES")==INK_SYSTEM_NES&&inkRomSystem("Game.md")==INK_SYSTEM_NONE);
    assert(!inkRomFilename("../game.nes")&&!inkRomFilename(".rom.gen")&&!inkRomFilename("a.smd"));
    assert(!inkRomSizeValid((InkSystem)3,65536));
    testRomFormats();testNes();testDispatch();puts("Console core suite PASS");
}
