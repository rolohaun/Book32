#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nofrendo.h>

// Exercise the exact generated PPU with and without internal scanline staging.
// All patterns are synthetic; no external game data is used.
void testNesPpuRows(void) {
    nes_t* nes=nes_getptr();ppu_t* p=nes->ppu;
    const ppu_t original=*p;
    const size_t bytes=NES_SCREEN_PITCH*NES_SCREEN_HEIGHT;
    uint8_t *a=malloc(bytes+8),*b=malloc(bytes+8);assert(a&&b);
    for(int i=0;i<8192;i++)nes->cart->chr_ram[i]=(i*29+(i>>3)*17)&255;
    for(int i=0;i<4096;i++)p->nametab[i]=(i*43+(i>>5)*7)&255;
    for(int i=0;i<32;i++)p->palette[i]=i*7&63;
    for(int i=0;i<64;i++) {
        p->oam[i*4]=(i/8)*28;p->oam[i*4+1]=i*11;
        p->oam[i*4+2]=(i&3)|((i&4)?OAMF_HFLIP:0)|((i&8)?OAMF_VFLIP:0)|((i&16)?OAMF_BEHIND:0);
        p->oam[i*4+3]=(i%8==7)?255:(i%8)*29;
    }
    for(int mode=0;mode<16;mode++)for(int scan=0;scan<240;scan++) {
        p->bg_on=!(mode&1);p->obj_on=!(mode&2);
        p->left_bg_on=!!(mode&4);p->left_obj_on=!!(mode&8);
        p->obj_height=(mode&4)?16:8;p->tile_xofs=scan&7;
        p->options[PPU_DRAW_BACKGROUND]=!(mode==14);p->options[PPU_DRAW_SPRITES]=!(mode==15);
        p->options[PPU_LIMIT_SPRITES]=!(mode&8);
        p->strikeflag=false;p->strike_cycle=~0u;p->vaddr=p->vaddr_latch=0x2000+(scan%30)*32;
        const ppu_t before=*p;
        memset(a,0xa5,bytes+8);memset(b,0xa5,bytes+8);
        inkNesStageRows=false;ppu_renderline(a+4,scan,true);ppu_endline();
        const ppu_t expected=*p;
        *p=before;inkNesStageRows=true;ppu_renderline(b+4,scan,true);ppu_endline();
        assert(!memcmp(a,b,bytes+8));assert(!memcmp(p,&expected,sizeof(*p)));
        for(int i=0;i<4;i++)assert(b[i]==0xa5&&b[bytes+4+i]==0xa5);
    }
    *p=original;inkNesStageRows=true;free(a);free(b);
    puts("NES PPU PASS: staged/original pixels and sprite flags, clipping, scroll, 8x16 sprites and disabled layers");
}
