#include <assert.h>
#include <stdio.h>
#include "../../lib/Apps/AppPaperboy/RomFormat.h"
void testRomFormats(void) {
    uint8_t h[512]={0};
    const char* valid[]={"one.gb","two.GBC","three.NES"};
    const char* invalid[]={"four.md","five.GEN","six.BIN",".gb","../x.nes","dir/x.md","dir\\x.bin","x.gen.state0","x.zip","x.smd","x:nes","x.md "};
    for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);i++)assert(inkRomFilename(valid[i]));
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!inkRomFilename(invalid[i]));
    memcpy(h,"NES\x1a",4);h[4]=1;
    assert(!inkRomValidateSystem(INK_SYSTEM_NES,h,16400));
    assert(inkRomValidateSystem(INK_SYSTEM_NES,h,16399));
    h[7]=8;assert(inkRomValidateSystem(INK_SYSTEM_NES,h,16400)); // NES 2.0
    h[7]=1;assert(inkRomValidateSystem(INK_SYSTEM_NES,h,16400)); // VS
    h[7]=0;h[6]=4;assert(!inkRomValidateSystem(INK_SYSTEM_NES,h,16912)); // Trainer
    h[0]=0;assert(inkRomValidateSystem(INK_SYSTEM_NES,h,16912));
    assert(inkRomValidateSystem((InkSystem)3,h,65536));
    assert(!inkRomSizeValid((InkSystem)3,65536) && inkRomLimit((InkSystem)3)==0);
    assert(inkRomValidateSystem(INK_SYSTEM_GB,NULL,32768));
    assert(inkRomValidateSystem(INK_SYSTEM_NONE,h,65536));
    puts("ROM validation PASS: extensions, paths, header formats, size limits and retired-system rejection");
}
