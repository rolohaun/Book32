#pragma once
#include <stdint.h>
#include <string.h>

// Per-pixel pulse tracking, inspired by Modos Smooth Graphics (MSG).
// MSG: Copyright 2026 Wenting Zhang, MIT license; see MSG-LICENSE.
// This scalar implementation is specific to InkDeck's FastEPD integration.
namespace InkDeckVideoPulse {
// State: desired white bit in bit 0, remaining pulses in bits 1..3.
// A settled black/white pixel is therefore 0/1. A direction change begins a
// fresh transition; unchanged pixels finish their pending pulses. Six pulses
// at 60 scans/s allow about 100 ms of settling; four remain the smooth profile.
// Experimental settled boost adds two pulses ONLY when reversing a pixel whose
// previous pulse budget completed. This is a software state, not a measurement
// of optical settling or idle duration. Unchanged settled pixels remain neutral.
constexpr uint8_t transitionPulses(uint8_t state, uint8_t pulses, bool settledBoost) {
    return pulses == 6 ? (settledBoost && (state >> 1) == 0 ? 8 : 6) : 4;
}
constexpr uint8_t drive(uint8_t state, bool white) {
    return ((state & 1) != white || (state >> 1) != 0) ? (white ? 2 : 1) : 0;
}
constexpr uint8_t next(uint8_t state, bool white, uint8_t pulses = 4, bool settledBoost = false) {
    return (white ? 1 : 0) | (((state & 1) != white ? transitionPulses(state,pulses,settledBoost) - 1 :
                              (state >> 1) ? (state >> 1) - 1 : 0) << 1);
}
inline void buildRow(uint8_t* output, const uint8_t* source, uint8_t* history,
                     int panelWidth, int x, int width, bool resetHistory, uint8_t pulses = 4,
                     bool settledBoost = false) {
    memset(output, 0, panelWidth / 4);
    for (int end = x + width; x < end; ++x, ++history) {
        const bool white = (source[x >> 3] & (0x80 >> (x & 7))) != 0;
        if (resetHistory) *history = white;
        const uint8_t push = drive(*history, white);
        *history = next(*history, white, pulses, settledBoost);
        output[x >> 2] |= push << (6 - 2 * (x & 3));
    }
}
// Two four-bit states per byte; both transitions and drive codes come from
// one 1024-entry table. History storage stays unchanged at two pixels per byte.
struct PackedLut {
    uint16_t entries[1024];
    explicit PackedLut(uint8_t pulses = 4, bool settledBoost = false) {
        for (int colors=0; colors<4; ++colors) for (int states=0; states<256; ++states) {
            const bool first=(colors&2)!=0, second=(colors&1)!=0;
            entries[(colors<<8)|states] = next(states&15,first,pulses,settledBoost) |
                (next(states>>4,second,pulses,settledBoost)<<4) |
                (((drive(states&15,first)<<2)|drive(states>>4,second))<<8);
        }
    }
    void buildRow(uint8_t* output, const uint8_t* source, uint8_t* history,
                  int panelWidth, int x, int width, bool resetHistory) const {
        memset(output,0,panelWidth/4);
        for (int end=x+width; x<end; x+=4,history+=2) {
            const uint8_t incoming=(source[x>>3] >> ((x&4) ? 0 : 4))&15;
            if (resetHistory) {
                history[0]=((incoming>>3)&1)|(((incoming>>2)&1)<<4);
                history[1]=((incoming>>1)&1)|((incoming&1)<<4);
            } else {
                const uint16_t a=entries[((incoming>>2)<<8)|history[0]];
                const uint16_t b=entries[((incoming&3)<<8)|history[1]];
                history[0]=a&255; history[1]=b&255;
                output[x>>2]=((a>>8)<<4)|(b>>8);
            }
        }
    }
    // Process a byte-aligned span of a compact row directly into an inactive
    // DMA buffer. Callers preserve the neutral border and 16-byte line padding.
    void buildSpan(uint8_t* output, const uint8_t* source, uint8_t* history,
                   int sourceBytes, bool resetHistory) const {
        for (int i = 0; i < sourceBytes; ++i, history += 4, output += 2) {
            const uint8_t incoming = source[i];
            if (resetHistory) {
                for (int pair = 0; pair < 4; ++pair)
                    history[pair] = ((incoming >> (7 - pair*2)) & 1) |
                                    (((incoming >> (6 - pair*2)) & 1) << 4);
                output[0] = output[1] = 0;
            } else {
                const uint16_t a = entries[((incoming >> 6) << 8) | history[0]];
                const uint16_t b = entries[(((incoming >> 4) & 3) << 8) | history[1]];
                const uint16_t c = entries[(((incoming >> 2) & 3) << 8) | history[2]];
                const uint16_t d = entries[((incoming & 3) << 8) | history[3]];
                history[0] = a & 255; history[1] = b & 255;
                history[2] = c & 255; history[3] = d & 255;
                output[0] = ((a >> 8) << 4) | (b >> 8);
                output[1] = ((c >> 8) << 4) | (d >> 8);
            }
        }
    }
};

// The same schedule is exercised by the scratch-buffer DMA-lifetime test.
// send() waits for the preceding transfer, then starts reading the supplied
// buffer asynchronously. We never modify that buffer until a later send has
// waited for it. Three dedicated buffers keep line padding and dummy rows safe.
template<typename Send>
inline void scanTriples(const PackedLut& lut, const uint8_t* source, uint8_t* history,
                        uint8_t* buffers, int stride, int panelHeight,
                        int x, int y, int width, int height, bool reset, Send send) {
    const int rowBytes = width / 8, stateBytes = width / 2;
    const int chunkBytes = rowBytes / 3;
    uint8_t* rows[2] = {buffers, buffers + stride};
    uint8_t* neutral = buffers + stride * 2;
    memset(buffers, 0, stride * 3); // Caller waited for any previous scan.
    lut.buildSpan(rows[0] + x/4, source, history, rowBytes, reset);
    int physicalRow = 0;
    for (; physicalRow < y; ++physicalRow) send(neutral, physicalRow);
    for (int group = 0; group < height/3; ++group) {
        uint8_t* current = rows[group & 1];
        uint8_t* next = rows[(group + 1) & 1];
        for (int repeat = 0; repeat < 3; ++repeat) {
            send(current, physicalRow++);
            // Calculate one third of the next row while this row is in flight.
            if (group + 1 < height/3) {
                const int offset = repeat * chunkBytes;
                lut.buildSpan(next + x/4 + offset*2,
                              source + (group+1)*rowBytes + offset,
                              history + (group+1)*stateBytes + offset*4,
                              chunkBytes, reset);
            }
        }
    }
    for (; physicalRow < panelHeight; ++physicalRow) send(neutral, physicalRow);
}
// Tests run at compile time on the exact helper used by the display driver.
static_assert(drive(1, false) == 1 && next(1, false) == 6, "white -> black");
static_assert(next(6, false) == 4 && next(4, false) == 2 && next(2, false) == 0,
              "black settles after exactly four scans");
static_assert(drive(0, false) == 0 && next(0, false) == 0, "settled black is neutral");
static_assert(drive(0, true) == 2 && next(0, true) == 7, "black -> white");
static_assert(next(7, true) == 5 && next(5, true) == 3 && next(3, true) == 1,
              "white settles after exactly four scans");
static_assert(drive(1, true) == 0 && next(1, true) == 1, "settled white is neutral");
static_assert(next(4, true) == 7 && next(5, false) == 6, "mid-transition reversal");
static_assert(next(1, false, 6) == 10 && next(10, false, 6) == 8 &&
              next(8, false, 6) == 6 && next(6, false, 6) == 4 &&
              next(4, false, 6) == 2 && next(2, false, 6) == 0,
              "black settles after exactly six scans");
static_assert(next(0, true, 6) == 11 && next(11, true, 6) == 9 &&
              next(9, true, 6) == 7 && next(7, true, 6) == 5 &&
              next(5, true, 6) == 3 && next(3, true, 6) == 1,
              "white settles after exactly six scans");
static_assert(next(8, true, 6) == 11 && next(9, false, 6) == 10,
              "six-pulse profile restarts on a direction reversal");
static_assert(next(0, true, 6, true) == 15 && next(1, false, 6, true) == 14,
              "settled reversal uses eight pulses and still fits a nibble");
static_assert(next(8, true, 6, true) == 11 && next(9, false, 6, true) == 10,
              "interrupted transitions retain the original six-pulse budget");
static_assert(next(0, false, 6, true) == 0 && next(1, true, 6, true) == 1,
              "settled boost never periodically re-drives unchanged pixels");
static_assert(next(0, true, 4, true) == 7 && next(1, false, 4, true) == 6,
              "four-pulse baseline cannot be boosted");
}
