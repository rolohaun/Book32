#pragma once
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBoldOblique18pt7b.h>
#include "GameViewport.h"
#include "NesViewport.h"
#include "RomFormat.h"

// Shared drawing + hit geometry. Static decoration is painted only on entry /
// resume; the video driver touches only the system's game window.
namespace InkBoyUi {
constexpr int DPAD_X = 120, DPAD_Y = 674, DPAD_HALF = 88, DPAD_ARM = 29;
constexpr int A_X = 443, A_Y = 630, B_X = 338, B_Y = 681, AB_RADIUS = 43;
constexpr int SELECT_X = 174, START_X = 285, SMALL_Y = 810;
constexpr int SMALL_LENGTH = 82, SMALL_HEIGHT = 26;
constexpr uint16_t BLACK = 0, WHITE = 1;
constexpr int controlOffset(InkSystem system) { return system==INK_SYSTEM_NES ? 12 : 0; }

inline bool circleHit(int x, int y, int cx, int cy, int r) {
    return (x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r;
}
inline bool capsuleHit(int x, int y, int cx, int cy, int length, int height, int pad = 0) {
    // Inverse rotation, -25 degrees. Fixed point avoids per-pixel trig.
    int dx=x-cx, dy=y-cy;
    int u=(906*dx-423*dy)/1000, v=(423*dx+906*dy)/1000;
    int segment=(length-height)/2, r=height/2+pad;
    int end=u < -segment ? -segment : u > segment ? segment : u;
    return (u-end)*(u-end)+v*v <= r*r;
}
inline uint16_t buttonsAt(int x, int y,InkSystem system=INK_SYSTEM_GB) {
    uint16_t buttons=0;
    const int offset=controlOffset(system), padY=DPAD_Y+offset;
    // The full D-pad square intentionally supports diagonal combinations.
    if (x>=DPAD_X-DPAD_HALF && x<=DPAD_X+DPAD_HALF &&
        y>=padY-DPAD_HALF && y<=padY+DPAD_HALF) {
        if (x<DPAD_X-DPAD_ARM) buttons|=0x20;
        if (x>DPAD_X+DPAD_ARM) buttons|=0x10;
        if (y<padY-DPAD_ARM) buttons|=0x40;
        if (y>padY+DPAD_ARM) buttons|=0x80;
    }
    if (circleHit(x,y,A_X,A_Y+offset,AB_RADIUS+10)) buttons|=1;
    if (circleHit(x,y,B_X,B_Y+offset,AB_RADIUS+10)) buttons|=2;
    if (capsuleHit(x,y,SELECT_X,SMALL_Y+offset,SMALL_LENGTH,SMALL_HEIGHT,14)) buttons|=4;
    if (capsuleHit(x,y,START_X,SMALL_Y+offset,SMALL_LENGTH,SMALL_HEIGHT,14)) buttons|=8;
    return buttons;
}
inline uint16_t gray(int x, int y, int density) {
    static const uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    return bayer[y&3][x&3] < density ? BLACK : WHITE;
}
inline bool roundRectHit(int x,int y,int left,int top,int w,int h,int r) {
    if (x<left || x>=left+w || y<top || y>=top+h) return false;
    int cx=x<left+r ? left+r : x>=left+w-r ? left+w-r-1 : x;
    int cy=y<top+r ? top+r : y>=top+h-r ? top+h-r-1 : y;
    return circleHit(x,y,cx,cy,r);
}
inline void centeredText(Adafruit_GFX& d,const char* text,int cx,int top,const GFXfont* font,uint8_t size=1) {
    d.setFont(font); d.setTextSize(size); d.setTextColor(BLACK); d.setTextWrap(false);
    int16_t bx,by; uint16_t w,h;
    d.getTextBounds(text,0,0,&bx,&by,&w,&h);
    d.setCursor(cx-int(w)/2-bx,top-by); d.print(text);
    d.setFont(nullptr); d.setTextSize(1);
}
inline void slantedText(Adafruit_GFX& d,const char* text,int cx,int cy) {
    int width=int(strlen(text))*12;
    GFXcanvas1 glyph(width,16);
    if (!glyph.getBuffer()) { centeredText(d,text,cx,cy-8,nullptr,2); return; }
    glyph.fillScreen(WHITE); glyph.setTextColor(BLACK); glyph.setTextSize(2);
    glyph.setTextWrap(false); glyph.setCursor(0,0); glyph.print(text);
    for(int dy=-30;dy<=30;++dy) for(int dx=-width;dx<=width;++dx) {
        int u=(906*dx-423*dy)/1000+width/2, v=(423*dx+906*dy)/1000+8;
        if (u>=0 && u<width && v>=0 && v<16 && !glyph.getPixel(u,v))
            d.drawPixel(cx+dx,cy+dy,BLACK);
    }
}
inline void smallButton(Adafruit_GFX& d,int cx,const char* text,int offset=0) {
    const int cy=SMALL_Y+offset;
    for(int y=cy-34;y<=cy+34;++y) for(int x=cx-50;x<=cx+50;++x) {
        if (!capsuleHit(x,y,cx,cy,SMALL_LENGTH,SMALL_HEIGHT)) continue;
        bool inner=capsuleHit(x,y,cx,cy,SMALL_LENGTH-4,SMALL_HEIGHT-4);
        d.drawPixel(x,y,inner ? gray(x,y,8) : BLACK);
    }
    slantedText(d,text,cx+12,cy+33);
}
inline void draw(Adafruit_GFX& d,InkSystem system=INK_SYSTEM_GB) {
    using namespace GameViewport;
    const bool nes=system==INK_SYSTEM_NES;
    const int gx=nes ? NesViewport::X : X, gy=nes ? NesViewport::Y : Y;
    const int gw=nes ? NesViewport::W : W, gh=nes ? NesViewport::H : H;
    const int bx=nes ? 0 : 10, by=nes ? 44 : 60, bw=nes ? 524 : 504, bh=nes ? 500 : 466;
    const int offset=controlOffset(system), padY=DPAD_Y+offset;
    // Dark gray bezel: no gray-mode switch or additional video drive pulses.
    for(int y=by;y<by+bh;++y) for(int x=bx;x<bx+bw;++x) {
        if (!roundRectHit(x,y,bx,by,bw,bh,nes ? 4 : 12)) continue;
        if (x>=gx && x<gx+gw && y>=gy && y<gy+gh) continue;
        d.drawPixel(x,y,gray(x,y,12));
    }
    d.drawRect(gx-1,gy-1,gw+2,gh+2,BLACK);
    centeredText(d,"Ink Boy",103,nes ? 550 : 540,&FreeSansBoldOblique18pt7b);
    d.fillRoundRect(DPAD_X-DPAD_HALF,padY-DPAD_ARM,2*DPAD_HALF+1,2*DPAD_ARM+1,5,BLACK);
    d.fillRoundRect(DPAD_X-DPAD_ARM,padY-DPAD_HALF,2*DPAD_ARM+1,2*DPAD_HALF+1,5,BLACK);
    d.fillCircle(A_X,A_Y+offset,AB_RADIUS,BLACK); d.fillCircle(B_X,B_Y+offset,AB_RADIUS,BLACK);
    centeredText(d,"A",A_X,A_Y+offset+AB_RADIUS+14,&FreeSansBold12pt7b);
    centeredText(d,"B",B_X,B_Y+offset+AB_RADIUS+14,&FreeSansBold12pt7b);
    smallButton(d,SELECT_X,"SELECT",offset);
    smallButton(d,START_X,"START",offset);
    // Decorative speaker grille, not a promise of audio on this hardware.
    for(int bar=0;bar<5;++bar) {
        int x=355+bar*24, y=838-bar*10;
        for(int offset=-2;offset<=2;++offset) d.drawLine(x+offset,y,x+36+offset,y+66,BLACK);
    }
    centeredText(d,"Tap game screen to refresh",262,926,nullptr,1);
    d.setFont(nullptr); d.setTextSize(1); d.setTextWrap(true);
}
}
