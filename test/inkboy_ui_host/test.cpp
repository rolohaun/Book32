#include <assert.h>
#include <stdio.h>
#include "../../lib/Apps/AppPaperboy/InkBoyUi.h"
int main() {
    using namespace InkBoyUi;
    assert(buttonsAt(A_X,A_Y)==1 && buttonsAt(B_X,B_Y)==2);
    assert(buttonsAt(SELECT_X,SMALL_Y)==4 && buttonsAt(START_X,SMALL_Y)==8);
    assert(buttonsAt(DPAD_X-60,DPAD_Y)==0x20);
    assert(buttonsAt(DPAD_X+60,DPAD_Y)==0x10);
    assert(buttonsAt(DPAD_X,DPAD_Y-60)==0x40);
    assert(buttonsAt(DPAD_X,DPAD_Y+60)==0x80);
    assert(buttonsAt(DPAD_X-60,DPAD_Y-60)==0x60);
    assert(buttonsAt(DPAD_X,DPAD_Y)==0);
    assert((buttonsAt(DPAD_X+60,DPAD_Y)|buttonsAt(A_X,A_Y))==0x11);
    for(int y=0;y<LilygoLayout::HEIGHT;++y) for(int x=0;x<LilygoLayout::WIDTH;++x) {
        uint8_t mask=buttonsAt(x,y);
        assert((mask&3)!=3 && (mask&12)!=12); // A/B and Select/Start cannot overlap.
        if (y<GameViewport::Y+GameViewport::H) assert(mask==0);
        assert(!(mask&0xf0) || !(mask&0x0f)); // D-pad and button hit zones separate.
        uint16_t nes=buttonsAt(x,y,INK_SYSTEM_NES);
        assert((nes&3)!=3 && (nes&12)!=12);
        assert(!(nes&0xf0)||!(nes&0x0f));
        if(y<NesViewport::Y+NesViewport::H)assert(nes==0);
    }
    GFXcanvas1 canvas(LilygoLayout::WIDTH,LilygoLayout::HEIGHT);
    canvas.fillScreen(WHITE); InkBoyUi::draw(canvas);
    for(int y=GameViewport::Y;y<GameViewport::Y+GameViewport::H;++y)
        for(int x=GameViewport::X;x<GameViewport::X+GameViewport::W;++x)
            assert(canvas.getPixel(x,y)); // Trim never consumes any game pixels.
    assert(!canvas.getPixel(A_X,A_Y) && !canvas.getPixel(B_X,B_Y));
    assert(!canvas.getPixel(DPAD_X,DPAD_Y));
    canvas.fillScreen(WHITE);InkBoyUi::draw(canvas,INK_SYSTEM_NES);
    for(int y=NesViewport::Y;y<NesViewport::Y+NesViewport::H;++y)
        for(int x=NesViewport::X;x<NesViewport::X+NesViewport::W;++x)assert(canvas.getPixel(x,y));
    assert(buttonsAt(A_X,A_Y+12,INK_SYSTEM_NES)==1 && !canvas.getPixel(A_X,A_Y+12));
    assert(buttonsAt(B_X,B_Y+12,INK_SYSTEM_NES)==2 && !canvas.getPixel(B_X,B_Y+12));
    assert(buttonsAt(DPAD_X,DPAD_Y+12-60,INK_SYSTEM_NES)==0x40);
    assert(buttonsAt(SELECT_X,SMALL_Y+12,INK_SYSTEM_NES)==4);
    assert(buttonsAt(START_X,SMALL_Y+12,INK_SYSTEM_NES)==8);
    assert(!canvas.getPixel(DPAD_X,DPAD_Y+12));
    // Produce an exact native-resolution render from the same GFX code/fonts
    // used in firmware; placeholder game text is preview-only, not a ROM.
    centeredText(canvas,"< Pause / saves",110,24,nullptr,2);
    centeredText(canvas,"Nofrendo / NES",415,24,nullptr,1);
    centeredText(canvas,"GAME WINDOW",262,242,nullptr,2);
    centeredText(canvas,"256 x 240 native -> 512 x 480",262,272,nullptr,1);
    printf("P5\n%d %d\n255\n",canvas.width(),canvas.height());
    for(int y=0;y<canvas.height();++y) for(int x=0;x<canvas.width();++x)
        putchar(canvas.getPixel(x,y) ? 255 : 0);
    fprintf(stderr,"Ink Boy UI PASS: buttons, diagonals, multitouch masks, non-overlap, game-window isolation\n");
}
