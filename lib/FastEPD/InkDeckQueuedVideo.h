#pragma once
#include "InkDeckVideoPulse.h"

namespace InkDeckVideoPulse {
// Original video4 overlap schedule, with error propagation and a bounded wait
// supplied by the transport. No buffers are changed after a failed transfer.
template<typename Send, typename Wait>
inline bool scanTriplesChecked(const PackedLut& lut, const uint8_t* source, uint8_t* history,
                               uint8_t* buffers, int stride, int panelHeight,
                               int x, int y, int width, int height, bool reset,
                               Send send, Wait wait, bool neutralTail = false, int rowRepeat = 3) {
    const int rowBytes = width / 8, stateBytes = width / 2;
    if(rowRepeat<1 || height%rowRepeat || rowBytes%rowRepeat) return false;
    const int chunkBytes = rowBytes / rowRepeat;
    uint8_t* rows[2] = {buffers, buffers + stride};
    uint8_t* neutral = buffers + 2 * stride;
    unsigned submitted = 0;
    int physicalRow = 0;
    memset(buffers, 0, stride * 3);
    lut.buildSpan(rows[0] + x / 4, source, history, rowBytes, reset);
    const auto submit = [&](uint8_t* row) {
        if (!wait(submitted) || !send(row, physicalRow)) return false;
        ++submitted;
        ++physicalRow;
        return true;
    };
    for (; physicalRow < y;) if (!submit(neutral)) return false;
    for (int group = 0; group < height / rowRepeat; ++group) {
        uint8_t* current = rows[group & 1];
        uint8_t* next = rows[(group + 1) & 1];
        for (int repeat = 0; repeat < rowRepeat; ++repeat) {
            if (!submit(current)) return false;
            if (group + 1 < height / rowRepeat) {
                const int offset = repeat * chunkBytes;
                lut.buildSpan(next + x / 4 + offset * 2,
                              source + (group + 1) * rowBytes + offset,
                              history + (group + 1) * stateBytes + offset * 4,
                              chunkBytes, reset);
            }
        }
    }
    for (; physicalRow < panelHeight;) if (!submit(neutral)) return false;
    // One optional, all-neutral transaction beyond the visible rows. It must
    // not advance any pixel history, and must drain before the next scan.
    if (neutralTail && !submit(neutral)) return false;
    return wait(submitted);
}

// Three outstanding rows fit the four-descriptor ESP-IDF transaction pool.
// wait(n) means that transaction n (one-based) has completely stopped reading
// its buffer. send() must not modify a submitted buffer or advance panel rows;
// the transport's completion callback handles the latter before the next DMA.
template<typename Send, typename Wait>
inline bool queueTriples(const PackedLut& lut, const uint8_t* source, uint8_t* history,
                         uint8_t* buffers, int stride, int panelHeight,
                         int x, int y, int width, int height, bool reset,
                         Send send, Wait wait, bool neutralTail = false, int rowRepeat = 3) {
    constexpr unsigned depth = 3;
    const int rowBytes = width / 8, stateBytes = width / 2;
    if(rowRepeat<1 || height%rowRepeat) return false;
    uint8_t* rows[2] = {buffers, buffers + stride};
    uint8_t* neutral = buffers + 2 * stride;
    unsigned submitted = 0, lastUse[2] = {};
    int physicalRow = 0;
    memset(buffers, 0, stride * 3); // Previous scan must already be drained.
    const auto submit = [&](uint8_t* row) {
        if (submitted >= depth && !wait(submitted - depth + 1)) return false;
        if (!send(row, physicalRow)) return false;
        ++submitted;
        ++physicalRow;
        return true;
    };
    for (; physicalRow < y;) if (!submit(neutral)) return false;
    for (int group = 0; group < height / rowRepeat; ++group) {
        const int slot = group & 1;
        // All repeated reads must finish before this ping-pong slot can
        // be rewritten, even when the driver queue runs ahead of the panel.
        if (lastUse[slot] && !wait(lastUse[slot])) return false;
        lut.buildSpan(rows[slot] + x / 4, source + group * rowBytes,
                      history + group * stateBytes, rowBytes, reset);
        for (int repeat = 0; repeat < rowRepeat; ++repeat)
            if (!submit(rows[slot])) return false;
        lastUse[slot] = submitted;
    }
    for (; physicalRow < panelHeight;) if (!submit(neutral)) return false;
    if (neutralTail && !submit(neutral)) return false;
    return wait(submitted);
}

// Hardware-free regression test of the exact queued producer. The mock DMA
// retains all outstanding pointers, and checks every byte until completion.
// Different drain schedules include maximum backlog and immediate completion.
inline __attribute__((noinline)) bool queuedVideoProfileSelfTest(uint8_t pulses, bool settledBoost = false, int rowRepeat = 3) {
    PackedLut lut(pulses, settledBoost);
    constexpr int width = 48, groups = 4, stride = 32;
    const int height = groups*rowRepeat+6;
    uint8_t source[groups * width / 8], scalar[groups * width], packed[groups * width / 2];
    uint8_t buffers[3 * stride + 2], expected[groups][stride];
    struct Pending { const uint8_t* ptr; uint8_t copy[stride]; unsigned number; } pending[3];
    bool ok = true;
    for (int tail = 0; tail < 2; ++tail)
    for (int transport = 0; transport < 2; ++transport)
    for (int schedule = 0; schedule < 3; ++schedule) {
        for (int i = 0; i < groups * width; ++i) scalar[i] = i & 15;
        for (int i = 0; i < groups * width / 2; ++i)
            packed[i] = scalar[i * 2] | (scalar[i * 2 + 1] << 4);
        for (int scan = 0; scan < 12; ++scan) {
            buffers[0] = 0xa5; buffers[sizeof(buffers) - 1] = 0x5a;
            const bool reset = scan == 0 || scan == 8;
            for (int i = 0; i < (int)sizeof(source); ++i)
                source[i] = scan < 5 ? 0 : scan < 8 ? 255 : (i * 73 + scan * 29) & 255;
            for (int group = 0; group < groups; ++group) {
                uint8_t fullRow[8] = {};
                memcpy(fullRow + 1, source + group * width / 8, width / 8);
                memset(expected[group], 0, stride);
                buildRow(expected[group], fullRow, scalar + group * width, 64, 8, width, reset, pulses, settledBoost);
            }
            unsigned sent = 0, completed = 0;
            int count = 0;
            const auto intact = [&]() {
                for (int i = 0; i < count; ++i)
                    if (memcmp(pending[i].ptr, pending[i].copy, stride)) ok = false;
            };
            const auto complete = [&]() {
                intact();
                if (!count) { ok = false; return; }
                if (pending[0].number != completed + 1) ok = false;
                ++completed;
                for (int i = 1; i < count; ++i) pending[i - 1] = pending[i];
                --count;
            };
            const auto send = [&](uint8_t* row, int y) {
                    intact();
                    if (y != (int)sent || count >= 3) { ok = false; return false; }
                    for (int i = 0; i < stride; ++i) {
                        const uint8_t value = (y >= 3 && y < 3+groups*rowRepeat) ? expected[(y - 3) / rowRepeat][i] : 0;
                        if (row[i] != value) ok = false;
                    }
                    pending[count].ptr = row;
                    memcpy(pending[count].copy, row, stride);
                    pending[count++].number = ++sent;
                    if (schedule == 1 || (schedule == 2 && ((sent + scan) % 3 == 0))) complete();
                    return true;
                };
            const auto wait = [&](unsigned goal) {
                    intact();
                    if (goal > sent) { ok = false; return false; }
                    while (completed < goal) complete();
                    return true;
                };
            const bool success = transport == 0 ?
                queueTriples(lut, source, packed, buffers + 1, stride,
                    height, 8, 3, width, groups * rowRepeat, reset, send, wait, tail, rowRepeat) :
                scanTriplesChecked(lut, source, packed, buffers + 1, stride,
                    height, 8, 3, width, groups * rowRepeat, reset, send, wait, tail, rowRepeat);
            if (!success || sent != height + tail || completed != height + tail || count) ok = false;
            if (buffers[0] != 0xa5 || buffers[sizeof(buffers) - 1] != 0x5a) ok = false;
            for (int i = 0; i < groups * width / 2; ++i)
                if (packed[i] != (scalar[i * 2] | (scalar[i * 2 + 1] << 4))) ok = false;
        }
    }
    // Inject failures at every send position and every wait goal. Once an
    // operation fails, neither scheduler may mutate DMA buffers or history.
    for (int tail = 0; tail < 2; ++tail)
    for (int transport = 0; transport < 2; ++transport)
    for (int failWait = 0; failWait < 2; ++failWait)
    for (unsigned failure = 0; failure <= height + tail; ++failure) {
        memset(source, 0, sizeof(source));
        memset(packed, 17, sizeof(packed)); // Two settled white pixels.
        uint8_t stoppedBuffers[sizeof(buffers)], stoppedHistory[sizeof(packed)];
        bool stopped = false;
        const auto abort = [&]() {
            if (stopped) ok = false;
            stopped = true;
            memcpy(stoppedBuffers, buffers, sizeof(buffers));
            memcpy(stoppedHistory, packed, sizeof(packed));
            return false;
        };
        const auto send = [&](uint8_t*, int y) {
            if (stopped) ok = false;
            return !failWait && (unsigned)y == failure ? abort() : true;
        };
        const auto wait = [&](unsigned goal) {
            if (stopped) ok = false;
            return failWait && goal == failure ? abort() : true;
        };
        const bool success = transport == 0 ?
            queueTriples(lut, source, packed, buffers + 1, stride,
                height, 8, 3, width, groups * rowRepeat, false, send, wait, tail, rowRepeat) :
            scanTriplesChecked(lut, source, packed, buffers + 1, stride,
                height, 8, 3, width, groups * rowRepeat, false, send, wait, tail, rowRepeat);
        if (success == stopped) ok = false;
        if (stopped && (memcmp(stoppedBuffers, buffers, sizeof(buffers)) ||
                        memcmp(stoppedHistory, packed, sizeof(packed)))) ok = false;
    }
    return ok;
}
// Edge case: the game reaches the final visible row. The tail must not erase
// that row, repeat its black drive, or consume another source/history row.
inline __attribute__((noinline)) bool scanTailBoundarySelfTest() {
    PackedLut lut(6);
    constexpr int width = 24, height = 6, stride = 32;
    uint8_t source[6] = {}, history[24], buffers[3 * stride];
    for (int transport = 0; transport < 2; ++transport)
    for (int tail = 0; tail < 2; ++tail) {
        memset(history, 17, sizeof(history)); // Settled white -> black.
        unsigned sent = 0, drained = 0;
        bool ok = true;
        const auto send = [&](uint8_t* row, int y) {
            if (y != (int)sent || y >= height + tail) ok = false;
            for (int byte = 0; byte < stride; ++byte) {
                const uint8_t expected = y < height && byte >= 2 && byte < 8 ? 0x55 : 0;
                if (row[byte] != expected) ok = false;
            }
            ++sent;
            return true;
        };
        const auto wait = [&](unsigned goal) {
            if (goal > sent) ok = false;
            drained = goal;
            return true;
        };
        const bool success = transport == 0 ?
            queueTriples(lut, source, history, buffers, stride, height,
                8, 0, width, height, false, send, wait, tail) :
            scanTriplesChecked(lut, source, history, buffers, stride, height,
                8, 0, width, height, false, send, wait, tail);
        if (!success || !ok || sent != height + tail || drained != sent) return false;
        // Exactly one six-pulse state step, independent of the extra row.
        for (uint8_t state : history) if (state != 0xaa) return false;
    }
    return true;
}
inline bool queuedVideoSelfTest() {
    return queuedVideoProfileSelfTest(4) && queuedVideoProfileSelfTest(6) &&
           queuedVideoProfileSelfTest(6, true) && queuedVideoProfileSelfTest(6, false, 1) && scanTailBoundarySelfTest();
}
}
