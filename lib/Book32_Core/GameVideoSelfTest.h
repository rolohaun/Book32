#pragma once
#include "GameViewport.h"
#include "NesViewport.h"
#include "GameFrameTiming.h"
#include <InkDeckVideoPulse.h>
#include <InkDeckQueuedVideo.h>
#include <InkDeckNesVideo.h>

// Simulated asynchronous DMA: retain each transmitted pointer until the next
// send, checking it has not been modified while the transfer was in flight.
inline __attribute__((noinline)) bool gameVideoPipelineProfileSelfTest(uint8_t pulses, bool settledBoost = false) {
    InkDeckVideoPulse::PackedLut lut(pulses, settledBoost);
    constexpr int width=48, groups=3, stride=32, height=15;
    uint8_t source[groups*width/8], scalar[groups*width], packed[groups*width/2];
    uint8_t buffers[3*stride+2], expected[groups][stride], inFlightCopy[stride];
    buffers[0]=0xa5; buffers[sizeof(buffers)-1]=0x5a;
    for (int i=0; i<groups*width; ++i) scalar[i]=i&15;
    for (int i=0; i<groups*width/2; ++i) packed[i]=scalar[i*2]|(scalar[i*2+1]<<4);
    bool ok=true;
    for (int scan=0; scan<12; ++scan) {
        const bool reset=scan==0 || scan==8;
        for (int i=0; i<(int)sizeof(source); ++i)
            source[i]=scan<5 ? 0 : scan<8 ? 255 : (i*73 + scan*29)&255;
        for (int group=0; group<groups; ++group) {
            uint8_t fullRow[8]={};
            memcpy(fullRow+1, source+group*width/8, width/8);
            memset(expected[group],0,stride);
            InkDeckVideoPulse::buildRow(expected[group],fullRow,scalar+group*width,64,8,width,reset,pulses,settledBoost);
        }
        const uint8_t* inFlight=nullptr;
        int count=0;
        InkDeckVideoPulse::scanTriples(lut,source,packed,buffers+1,stride,height,8,3,width,groups*3,reset,
            [&](uint8_t* row,int y) {
                if (inFlight && memcmp(inFlight,inFlightCopy,stride)) ok=false;
                if (y!=count++) ok=false;
                for (int i=0; i<stride; ++i) {
                    const uint8_t value=(y>=3 && y<12) ? expected[(y-3)/3][i] : 0;
                    if (row[i]!=value) ok=false;
                }
                inFlight=row; memcpy(inFlightCopy,row,stride);
            });
        if (count!=height || (inFlight && memcmp(inFlight,inFlightCopy,stride))) ok=false;
        if (buffers[0]!=0xa5 || buffers[sizeof(buffers)-1]!=0x5a) ok=false;
        for (int i=0; i<groups*width/2; ++i)
            if (packed[i]!=(scalar[i*2]|(scalar[i*2+1]<<4))) ok=false;
    }
    return ok;
}
inline bool gameVideoPipelineSelfTest() {
    return gameVideoPipelineProfileSelfTest(4) && gameVideoPipelineProfileSelfTest(6) &&
           gameVideoPipelineProfileSelfTest(6, true);
}

// Keep each 2 KiB LUT local to one completed test invocation, rather than
// retaining multiple profile LUTs on the small startup stack simultaneously.
inline __attribute__((noinline)) bool gameVideoPulseProfileSelfTest(uint8_t pulses, bool settledBoost = false) {
    InkDeckVideoPulse::PackedLut packed(pulses, settledBoost);
    const int settledPulses = settledBoost && pulses == 6 ? 8 : pulses;
    uint8_t row[8], pixels[4], history[16];
    uint8_t packedHistory[8], packedRow[8], spanHistory[8], spanRow[8];
    bool ok = true;
    for (int white=0; white<2; ++white) {
        memset(pixels,white ? 0xff : 0,sizeof(pixels));
        memset(history,!white,sizeof(history));
        memset(packedHistory,white ? 0 : 17,sizeof(packedHistory));
        for (int scan=0; scan<10; ++scan) {
            InkDeckVideoPulse::buildRow(row,pixels,history,32,8,16,false,pulses,settledBoost);
            packed.buildRow(packedRow,pixels,packedHistory,32,8,16,false);
            if (memcmp(row,packedRow,sizeof(row))) ok=false;
            for (int x=0; x<32; ++x) {
                int expected=(x>=8 && x<24 && scan<settledPulses) ? (white ? 2 : 1) : 0;
                if (((row[x>>2]>>(6-2*(x&3)))&3)!=expected) ok=false;
            }
        }
        // Reset must initialize every nibble before table lookup, even when
        // storage initially contains 0xff, and produce only neutral output.
        memset(packedHistory,0xff,sizeof(packedHistory));
        memset(spanHistory,0xff,sizeof(spanHistory));
        memset(spanRow,0,sizeof(spanRow));
        packed.buildRow(packedRow,pixels,packedHistory,32,8,16,true);
        packed.buildSpan(spanRow+2,pixels+1,spanHistory,2,true);
        for (uint8_t value : packedRow) if (value) ok=false;
        for (uint8_t value : spanRow) if (value) ok=false;
        for (uint8_t value : packedHistory) if (value!=(white ? 17 : 0)) ok=false;
        if (memcmp(packedHistory,spanHistory,sizeof(packedHistory))) ok=false;

        // Stopping never periodically re-drives already unchanged pixels.
        for (int idle=0; idle<100; ++idle) {
            packed.buildRow(packedRow,pixels,packedHistory,32,8,16,false);
            for (uint8_t value : packedRow) if (value) ok=false;
        }
        // Reverse at every intermediate pulse and after completion/idle. Only
        // the completed transition may use the boost; a moving reversal stays
        // at the original pulse budget. Test both black/white directions.
        for (int cut=1; cut<=settledPulses+3; ++cut) {
            memset(packedHistory,white ? 0 : 17,sizeof(packedHistory));
            memset(pixels,white ? 0xff : 0,sizeof(pixels));
            for (int i=0; i<cut; ++i)
                packed.buildRow(packedRow,pixels,packedHistory,32,8,16,false);
            memset(pixels,white ? 0 : 0xff,sizeof(pixels));
            const int expectedPulses = cut >= settledPulses ? settledPulses : pulses;
            for (int scan=0; scan<settledPulses+2; ++scan) {
                packed.buildRow(packedRow,pixels,packedHistory,32,8,16,false);
                for (int x=0; x<32; ++x) {
                    int expected=(x>=8 && x<24 && scan<expectedPulses) ? (white ? 1 : 2) : 0;
                    if (((packedRow[x>>2]>>(6-2*(x&3)))&3)!=expected) ok=false;
                }
            }
        }
    }
    // Exhaust all 256 two-nibble state pairs and four input pairs, including
    // out-of-profile counters, against the scalar reference for this profile.
    for (int states=0; states<256; ++states) for (int colors=0; colors<4; ++colors) {
        memset(pixels,colors*0x55,sizeof(pixels));
        for (int i=0; i<16; i+=2) { history[i]=states&15; history[i+1]=states>>4; }
        memset(packedHistory,states,sizeof(packedHistory));
        memcpy(spanHistory,packedHistory,sizeof(spanHistory));
        memset(spanRow,0,sizeof(spanRow));
        InkDeckVideoPulse::buildRow(row,pixels,history,32,8,16,false,pulses,settledBoost);
        packed.buildRow(packedRow,pixels,packedHistory,32,8,16,false);
        packed.buildSpan(spanRow+2,pixels+1,spanHistory,2,false);
        if (memcmp(row,packedRow,sizeof(row))) ok=false;
        if (memcmp(row,spanRow,sizeof(row)) || memcmp(packedHistory,spanHistory,sizeof(spanHistory))) ok=false;
        for (int i=0; i<8; ++i)
            if (packedHistory[i] != (history[i*2] | (history[i*2+1]<<4))) ok=false;
    }
    return ok;
}

inline __attribute__((noinline)) bool nesVideoSelfTest() {
    using namespace NesViewport;
    uint8_t* source=(uint8_t*)ps_malloc(SOURCE_W*SOURCE_H);
    uint8_t* packed=(uint8_t*)ps_malloc(PACKED_BYTES+2);
    uint8_t* canvas=(uint8_t*)ps_malloc(GameViewport::PITCH*GameViewport::PANEL_H);
    if(!source || !packed || !canvas) { free(source);free(packed);free(canvas);return false; }
    for(int y=0;y<SOURCE_H;++y) for(int x=0;x<SOURCE_W;++x) source[y*SOURCE_W+x]=(x*3+y*7)&3;
    packed[0]=0x69;packed[PACKED_BYTES+1]=0x96;
    bool ok=true;
    for(uint8_t rotation : {1,3}) {
        memset(canvas,0xa5,GameViewport::PITCH*GameViewport::PANEL_H);
        pack2x(packed+1,source,rotation);blitPacked(canvas,packed+1,rotation);
        for(int ny=0;ny<GameViewport::PANEL_H;++ny) for(int nx=0;nx<GameViewport::PANEL_W;++nx) {
            const int x=rotation==3 ? GameViewport::PANEL_H-1-ny : ny;
            const int y=rotation==3 ? nx : GameViewport::PANEL_W-1-nx;
            bool expected=(0xa5 & (0x80>>(nx&7)))!=0;
            if(x>=X && x<X+W && y>=Y && y<Y+H) {
                const uint8_t shade=source[((y-Y)/2)*SOURCE_W+(x-X)/2];
                // Independent reference for all four 2x2 shade masks.
                const uint8_t coverage[4]={0,1,11,15};
                expected=!(coverage[shade] & (1<<(((y-Y)&1)*2+((x-X)&1))));
            }
            if(!!(canvas[ny*GameViewport::PITCH+nx/8] & (0x80>>(nx&7)))!=expected) ok=false;
        }
    }
    ok=ok && packed[0]==0x69 && packed[PACKED_BYTES+1]==0x96;
    free(source);free(packed);free(canvas);return ok;
}

// Runs once in the experimental LILYGO build, entirely on scratch buffers.
// It never drives the display or opens/writes files.
inline bool gameVideoSelfTest() {
    using namespace GameViewport;
    uint8_t* scratch = (uint8_t*)ps_malloc(PITCH * PANEL_H);
    uint8_t* source = (uint8_t*)ps_malloc(SOURCE_W * SOURCE_H);
    uint8_t* compact = (uint8_t*)ps_malloc(PACKED_BYTES);
    if (!scratch || !source || !compact) { free(scratch); free(source); free(compact); return false; }
    for (int y=0; y<SOURCE_H; ++y) for (int x=0; x<SOURCE_W; ++x)
        source[y*SOURCE_W+x] = (x*13+y*7)&3;
    bool ok = true;
    for (uint8_t rotation : {1,3}) {
        memset(scratch,0xa5,PITCH*PANEL_H);
        pack3x(compact,source,rotation);
        blitPacked(scratch,compact,rotation);
        for (int ny=0; ny<PANEL_H && ok; ++ny) for (int nx=0; nx<PANEL_W; ++nx) {
            const int x=rotation==3 ? PANEL_H-1-ny : ny;
            const int y=rotation==3 ? nx : PANEL_W-1-nx;
            bool expected=(0xa5 & (0x80>>(nx&7))) != 0;
            if (x>=X && x<X+W && y>=Y && y<Y+H) {
                uint8_t shade=source[((y-Y)/SCALE)*SOURCE_W+(x-X)/SCALE];
                expected=((y-Y)%SCALE)>=shade;
            }
            if (((scratch[ny*PITCH+(nx>>3)] & (0x80>>(nx&7))) != 0) != expected) {
                ok=false; break;
            }
        }
        // The low-memory fallback must produce the identical image as well.
        blit3x(scratch,source,rotation);
        for (int row=0; row<SOURCE_W; ++row) for (int repeat=0; repeat<SCALE; ++repeat)
            if (memcmp(scratch+(nativeY(rotation)+row*SCALE+repeat)*PITCH+nativeX(rotation)/8,
                       compact+row*ROW_BYTES,ROW_BYTES)) ok=false;
    }
    free(scratch); free(source); free(compact);
    return ok && nesVideoSelfTest() && GameFrameTiming::selfTest() && gameVideoPulseProfileSelfTest(4) && gameVideoPulseProfileSelfTest(6) &&
           gameVideoPulseProfileSelfTest(6, true) &&
           gameVideoPipelineSelfTest() && InkDeckVideoPulse::queuedVideoSelfTest() && InkDeckVideoPulse::nesPulseSelfTest();
}
