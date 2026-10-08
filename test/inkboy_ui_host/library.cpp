#include <assert.h>
#include <stdio.h>
#include "../../lib/Apps/AppPaperboy/InkBoyLibraryUi.h"
int main(){
    using namespace InkBoyLibraryUi;
    for(uint8_t mask=0;mask<16;mask+=2){
        int left=20,w=tabWidth(mask);
        for(int i=1;i<=3;i++)if(mask&(1<<i)){
            assert(tabAt(mask,left+w/2,TAB_Y+20)==i);left+=w+8;
        }
        for(int x=0;x<524;x++)assert(tabAt(mask,x,TAB_Y-1)==INK_SYSTEM_NONE);
    }
    for(int row=0;row<ROWS;row++){
        assert(rowAt(50,ROW_Y+row*ROW_STEP+20)==row);
        assert(rowAt(10,ROW_Y+row*ROW_STEP+20)==-1);
        assert(rowAt(50,ROW_Y+row*ROW_STEP+ROW_H)==-1);
    }
    GFXcanvas1 canvas(524,944);header(canvas,14,INK_SYSTEM_NES,7);
    card(canvas,0,"Pixel Quest - The Lost Cartridge","NES  /  RESUME SAVED GAME");
    card(canvas,1,"Star Runner","NES  /  TAP TO PLAY");
    card(canvas,2,"A Very Long Game Title That Needs Two Lines And An Ellipsis At The End","NES  /  TAP TO PLAY");
    card(canvas,3,"Night Flight","NES  /  TAP TO PLAY");
    card(canvas,4,"Pocket Adventure","NES  /  RESUME SAVED GAME");
    footer(canvas,0,2,"Tap a game to play or resume. Saves stay with each ROM.");
    printf("P5\n%d %d\n255\n",canvas.width(),canvas.height());
    for(int y=0;y<canvas.height();y++)for(int x=0;x<canvas.width();x++)putchar(canvas.getPixel(x,y)?255:0);
    fprintf(stderr,"Library PASS: conditional tabs, shared card hit geometry, pagination gaps\n");
}
