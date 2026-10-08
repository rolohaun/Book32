#pragma once
#include "GameViewport.h"
#include "NesHistoryMemory.h"
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define INK_NES_PACK_FAST IRAM_ATTR __attribute__((noinline))
#else
#define INK_NES_PACK_FAST
#endif

// NES keeps every 256x240 source pixel. Each becomes a 2x2 monochrome cell;
// ordered coverage preserves four gray levels without filtering adjacent pixels.
namespace NesViewport {
constexpr int SOURCE_W=256, SOURCE_H=240, SCALE=2;
constexpr int X=6, Y=48, W=512, H=480;
constexpr int ROW_BYTES=SOURCE_H/4, PACKED_BYTES=SOURCE_W*ROW_BYTES;
constexpr int HISTORY_BYTES=SOURCE_W*SOURCE_H*9/8;
constexpr int HISTORY_BANKS=NesHistoryMemory::BANKS, HISTORY_BANK_ROWS=SOURCE_W/HISTORY_BANKS;
static_assert(HISTORY_BYTES==NesHistoryMemory::BYTES && SOURCE_W%HISTORY_BANKS==0,"whole NES rows per bank");
constexpr int nativeX(uint8_t rotation) { return rotation==3 ? Y : GameViewport::PANEL_W-Y-H; }
constexpr int nativeY(uint8_t rotation) { return rotation==3 ? GameViewport::PANEL_H-X-W : X; }
constexpr bool black(uint8_t shade,int dx,int dy) {
    // Coverage: 0/4, 1/4, 3/4, 4/4. No temporal dithering or extra pulses.
    return shade==3 || (shade>=1 && dx==0 && dy==0) ||
           (shade==2 && ((dx==1 && dy==0) || (dx==1 && dy==1)));
}
static_assert(X+W<=LilygoLayout::WIDTH && Y+H<=LilygoLayout::HEIGHT,"NES fits safe portrait canvas");
static_assert((nativeX(1)&7)==0 && (nativeX(3)&7)==0 && (H&7)==0,"byte-aligned NES window");
static INK_NES_PACK_FAST void packIndexed(uint8_t* packed,const uint8_t* pixels,int pitch,const uint8_t* palette,uint8_t rotation) {
    if(!packed || !pixels || (rotation!=1 && rotation!=3)) return;
    // The queued display expands these lossless 2-bit shades directly into
    // paired 2x rows. Do not materialize a 61 KB intermediate gray image.
    // Keep only 16 source rows active in cache while walking across the image.
    // Full-height strips evicted the adjoining half-cache-lines before reuse,
    // especially with the PPU's eight-byte left overdraw. The compact output
    // and this small source band fit together; 4-byte stores are aligned.
    constexpr int TILE_W=32,TILE_H=16;
    uint8_t tile[TILE_W*TILE_H],row[TILE_H/4];
    for(int top=0;top<SOURCE_H;top+=TILE_H)for(int left=0;left<SOURCE_W;left+=TILE_W) {
      for(int y=0;y<TILE_H;++y)
        memcpy(tile+y*TILE_W,pixels+(top+y)*pitch+left,TILE_W);
      for(int column=0;column<TILE_W;++column) {
        const int outputColumn=rotation==3 ? SOURCE_W-1-left-column : left+column;
        for(int sy=0;sy<TILE_H;sy+=4) {
            uint8_t bits=0;
            for(int i=0;i<4;++i) {
                const int y=rotation==3 ? sy+i : TILE_H-1-sy-i;
                const uint8_t index=tile[y*TILE_W+column];
                const uint8_t shade=palette ? palette[index] : index&3;
                bits=(bits<<2)|shade;
            }
            row[sy/4]=bits;
        }
        const int outputByte=(rotation==3 ? top : SOURCE_H-TILE_H-top)/4;
        memcpy(packed+outputColumn*ROW_BYTES+outputByte,row,sizeof(row));
      }
    }
}
inline void pack2x(uint8_t* packed,const uint8_t* shades,uint8_t rotation) {
    packIndexed(packed,shades,SOURCE_W,nullptr,rotation);
}
inline void blitPacked(uint8_t* canvas,const uint8_t* packed,uint8_t rotation) {
    if(!canvas || !packed || (rotation!=1 && rotation!=3)) return;
    // Used only for entry/manual cleaning/fallback, never the normal fast scan.
    const uint8_t even[4]={3,uint8_t(rotation==3 ? 1 : 2),uint8_t(rotation==3 ? 1 : 2),0};
    const uint8_t odd[4]={3,3,0,0};
    for(int row=0;row<SOURCE_W;++row) {
        uint8_t* first=canvas+(nativeY(rotation)+row*2)*GameViewport::PITCH+nativeX(rotation)/8;
        uint8_t* second=first+GameViewport::PITCH;
        for(int b=0;b<ROW_BYTES;++b) {
            const uint8_t incoming=packed[row*ROW_BYTES+b];
            uint8_t a=0,c=0;
            for(int p=0;p<4;++p) { const int shade=(incoming>>(6-2*p))&3;a=(a<<2)|even[shade];c=(c<<2)|odd[shade]; }
            first[b]=rotation==3 ? c : a;second[b]=rotation==3 ? a : c;
        }
    }
}
}
#undef INK_NES_PACK_FAST
