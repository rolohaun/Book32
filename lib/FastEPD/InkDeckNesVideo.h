#pragma once
#include "InkDeckVideoPulse.h"
#include <new>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define INK_NES_SCAN_FAST IRAM_ATTR __attribute__((noinline))
#else
#define INK_NES_SCAN_FAST
#endif

namespace InkDeckVideoPulse {
// Six-pulse, three-threshold histories have only 304 reachable combinations.
// Encode those exact states in nine bits, not three independent four-bit fields.
// One lookup now advances all thresholds together, including mid-pulse reversals.
struct NesPackedLut {
    static constexpr unsigned COUNT=304;
    uint32_t entries[4*COUNT];
    uint16_t reset[4];
    // Four settled low-byte IDs for each incoming packed shade byte. This
    // saves unpacking eight cells individually when a whole block is neutral.
    uint32_t settledBytes[256];
    static const uint16_t* states() {
        static const uint16_t values[COUNT]={
        0x000,0x100,0x110,0x111,0x112,0x113,0x114,0x115,0x116,0x117,0x118,0x119,0x11a,0x11b,0x120,0x122,
        0x130,0x133,0x134,0x135,0x136,0x137,0x138,0x139,0x13a,0x13b,0x140,0x142,0x144,0x150,0x152,0x155,
        0x156,0x157,0x158,0x159,0x15a,0x15b,0x160,0x162,0x164,0x166,0x170,0x172,0x174,0x177,0x178,0x179,
        0x17a,0x17b,0x180,0x182,0x184,0x186,0x188,0x190,0x192,0x194,0x196,0x199,0x19a,0x19b,0x1a0,0x1a2,
        0x1a4,0x1a6,0x1a8,0x1aa,0x1b0,0x1b2,0x1b4,0x1b6,0x1b8,0x1bb,0x200,0x220,0x222,0x300,0x330,0x333,
        0x334,0x335,0x336,0x337,0x338,0x339,0x33a,0x33b,0x340,0x344,0x350,0x355,0x356,0x357,0x358,0x359,
        0x35a,0x35b,0x360,0x364,0x366,0x370,0x374,0x377,0x378,0x379,0x37a,0x37b,0x380,0x384,0x386,0x388,
        0x390,0x394,0x396,0x399,0x39a,0x39b,0x3a0,0x3a4,0x3a6,0x3a8,0x3aa,0x3b0,0x3b4,0x3b6,0x3b8,0x3bb,
        0x400,0x420,0x422,0x440,0x442,0x444,0x500,0x520,0x522,0x550,0x552,0x555,0x556,0x557,0x558,0x559,
        0x55a,0x55b,0x560,0x562,0x566,0x570,0x572,0x577,0x578,0x579,0x57a,0x57b,0x580,0x582,0x586,0x588,
        0x590,0x592,0x596,0x599,0x59a,0x59b,0x5a0,0x5a2,0x5a6,0x5a8,0x5aa,0x5b0,0x5b2,0x5b6,0x5b8,0x5bb,
        0x600,0x620,0x622,0x640,0x642,0x644,0x660,0x662,0x664,0x666,0x700,0x720,0x722,0x740,0x742,0x744,
        0x770,0x772,0x774,0x777,0x778,0x779,0x77a,0x77b,0x780,0x782,0x784,0x788,0x790,0x792,0x794,0x799,
        0x79a,0x79b,0x7a0,0x7a2,0x7a4,0x7a8,0x7aa,0x7b0,0x7b2,0x7b4,0x7b8,0x7bb,0x800,0x820,0x822,0x840,
        0x842,0x844,0x860,0x862,0x864,0x866,0x880,0x882,0x884,0x886,0x888,0x900,0x920,0x922,0x940,0x942,
        0x944,0x960,0x962,0x964,0x966,0x990,0x992,0x994,0x996,0x999,0x99a,0x99b,0x9a0,0x9a2,0x9a4,0x9a6,
        0x9aa,0x9b0,0x9b2,0x9b4,0x9b6,0x9bb,0xa00,0xa20,0xa22,0xa40,0xa42,0xa44,0xa60,0xa62,0xa64,0xa66,
        0xa80,0xa82,0xa84,0xa86,0xa88,0xaa0,0xaa2,0xaa4,0xaa6,0xaa8,0xaaa,0xb00,0xb20,0xb22,0xb40,0xb42,
        0xb44,0xb60,0xb62,0xb64,0xb66,0xb80,0xb82,0xb84,0xb86,0xb88,0xbb0,0xbb2,0xbb4,0xbb6,0xbb8,0xbbb,
        };
        return values;
    }
    static unsigned index(unsigned state) {
        unsigned lo=0,hi=COUNT;const auto* values=states();
        while(lo<hi) { const unsigned mid=(lo+hi)/2;if(values[mid]<state)lo=mid+1;else hi=mid; }
        return lo<COUNT && values[lo]==state ? lo : COUNT;
    }
    NesPackedLut() {
        for(unsigned shade=0;shade<4;++shade) {
            unsigned settled=0;
            for(unsigned t=0;t<3;++t)settled|=unsigned(shade<t+1)<<(4*t);
            reset[shade]=index(settled);
            for(unsigned id=0;id<COUNT;++id) {
                const unsigned old=states()[id];
                unsigned updated=0,push[3];
                for(unsigned t=0;t<3;++t) {
                    const unsigned s=(old>>(4*t))&15;const bool white=shade<t+1;
                    updated|=unsigned(next(s,white,6))<<(4*t);push[t]=drive(s,white);
                }
                // Canonical physical rows: B B, then A C (rotation 3).
                const unsigned dots=(push[1]*5<<4)|(push[0]<<2)|push[2];
                // Rotation 1 is C A, then B B. Preposition BOTH orientations
                // in the otherwise unused top byte, rather than reshuffling
                // drive bits for every pixel on every scan.
                const unsigned opposite=((push[2]<<2)|push[0])<<4|push[1]*5;
                entries[shade*COUNT+id]=index(updated)|(dots<<16)|(opposite<<24);
            }
        }
        for(unsigned colors=0;colors<256;++colors) {
            uint32_t ids=0;
            for(unsigned i=0;i<4;++i)ids|=uint32_t(reset[(colors>>(6-2*i))&3])<<(8*i);
            settledBytes[colors]=ids;
        }
    }
};
inline unsigned nesHistoryId(const uint8_t* history,unsigned pixel) {
    const unsigned group=pixel/8,bit=pixel&7;
    return history[group*9+bit]|(((history[group*9+8]>>bit)&1)<<8);
}
static INK_NES_SCAN_FAST void buildNesRows(const NesPackedLut& lut,uint8_t* first,uint8_t* second,
                         const uint8_t* source,uint8_t* history,int pixels,
                         bool reset,bool reverseRows) {
    const unsigned shift=reverseRows ? 16 : 24;
    for(int group=0;group<pixels/8;++group,history+=9,source+=2,first+=4,second+=4) {
        const unsigned high=reset ? 0 : history[8];unsigned newHigh=0;
        // Low bytes alone are NOT sufficient: state IDs above 255 can share
        // them. Only all-zero high bits and exact settled IDs allow skipping.
        uint32_t lowA,lowB;
        if(!reset && high==0) {
            memcpy(&lowA,history,4);memcpy(&lowB,history+4,4);
            if(lowA==lut.settledBytes[source[0]] && lowB==lut.settledBytes[source[1]]) {
                memset(first,0,4);memset(second,0,4);continue;
            }
        }
        for(unsigned pair=0;pair<4;++pair) {
            const unsigned colors=(source[pair/2]>>((pair&1)?0:4))&15;
            const unsigned a=colors>>2,b=colors&3,bit=pair*2;
            uint32_t va,vb;
            if(reset) { va=lut.reset[a];vb=lut.reset[b]; }
            else {
                const unsigned ia=history[bit]|(((high>>bit)&1)<<8);
                const unsigned ib=history[bit+1]|(((high>>(bit+1))&1)<<8);
                if(ia==lut.reset[a] && ib==lut.reset[b]) {
                    // All thresholds are already settled at this exact shade.
                    // Keep the stored ID intact; emit neutral drive only.
                    newHigh|=high&(3U<<bit);first[pair]=second[pair]=0;continue;
                }
                va=lut.entries[a*NesPackedLut::COUNT+ia];
                vb=lut.entries[b*NesPackedLut::COUNT+ib];
            }
            history[bit]=va;history[bit+1]=vb;
            newHigh|=((va>>8)&1)<<bit;newHigh|=((vb>>8)&1)<<(bit+1);
            const unsigned da=(va>>shift)&255,db=(vb>>shift)&255;
            first[pair]=(da&0xf0)|(db>>4);
            second[pair]=((da&15)<<4)|(db&15);
        }
        history[8]=newHigh;
    }
}

enum class NesScanStage { Copy, Build, Send, Wait };
struct NoNesScanProfile {
    unsigned begin() const { return 0; }
    void end(NesScanStage,unsigned) const {}
};
struct StagedNesHistory {
    bool operator()(const uint8_t*) const { return false; }
};

// Two ping-pong row PAIRS plus a dedicated neutral row. The three-transaction
// queue keeps sending while the next pair is prepared. A pair is not writable
// until its final DMA read has completed. All failure paths stop immediately.
template<typename Send,typename Wait,typename Profile=NoNesScanProfile,typename Resident=StagedNesHistory>
inline bool queueNes(const NesPackedLut& lut,const uint8_t* source,uint8_t* history,
                     uint8_t* buffers,int stride,int panelHeight,int x,int y,
                     int width,int height,bool reset,bool reverseRows,
                     Send send,Wait wait,bool tail=false,bool queued=true,
                     uint8_t* const* banks=nullptr,int bankRows=32,Profile profile={},Resident resident={}) {
    if(width<=0 || width>960 || height<=0 || width%16 || height%2 || bankRows<=0) return false;
    const int pixels=width/2,sourceBytes=pixels/4,stateBytes=pixels*9/8;
    const unsigned depth=queued ? 3 : 1;
    uint8_t* neutral=buffers+4*stride;
    // External history uses an internal-stack tile to amortize cache misses.
    // Already-internal banks are updated in place, avoiding redundant copies.
    // History is private to this scan task, never read by DMA or the emulator.
    alignas(4) uint8_t localSource[960/8],localHistory[960*3/4];
    unsigned submitted=0,lastUse[2]={};int physicalRow=0;
    memset(buffers,0,stride*5);
    const auto complete=[&](unsigned goal) {
        const auto started=profile.begin();const bool ok=wait(goal);
        profile.end(NesScanStage::Wait,started);return ok;
    };
    const auto submit=[&](uint8_t* row) {
        if(submitted>=depth && !complete(submitted-depth+1))return false;
        const auto started=profile.begin();const bool ok=send(row,physicalRow);
        profile.end(NesScanStage::Send,started);if(!ok)return false;
        ++submitted;++physicalRow;return true;
    };
    for(;physicalRow<y;)if(!submit(neutral))return false;
    for(int group=0;group<height/2;++group) {
        const int slot=group&1;
        if(lastUse[slot] && !complete(lastUse[slot]))return false;
        uint8_t* first=buffers+slot*2*stride,*second=first+stride;
        auto started=profile.begin();
        memcpy(localSource,source+group*sourceBytes,sourceBytes);
        uint8_t* state=banks ? banks[group/bankRows]+(group%bankRows)*stateBytes : history+group*stateBytes;
        uint8_t* working=resident(state) ? state : localHistory;
        if(!reset && working!=state)memcpy(working,state,stateBytes);
        profile.end(NesScanStage::Copy,started);started=profile.begin();
        buildNesRows(lut,first+x/4,second+x/4,localSource,
                     working,pixels,reset,reverseRows);
        profile.end(NesScanStage::Build,started);started=profile.begin();
        if(working!=state)memcpy(state,working,stateBytes);
        profile.end(NesScanStage::Copy,started);
        if(!submit(first)||!submit(second))return false;
        lastUse[slot]=submitted;
    }
    for(;physicalRow<panelHeight;)if(!submit(neutral))return false;
    if(tail&&!submit(neutral))return false;
    return complete(submitted);
}

// Compare the compact three-threshold engine against independent physical-dot
// histories, then retain queued DMA pointers until completion to detect writes
// to any in-flight row. Covers both rotations, reset and gray reversals.
inline __attribute__((noinline)) bool nesPulseSelfTest() {
    constexpr int width=48,groups=4,stride=32,height=14,pixels=width/2;
    uint8_t source[groups*pixels/4],history[groups*pixels*9/8];
    uint8_t scalar[groups*2*width],expected[groups*2][stride],buffers[5*stride+2];
    struct Pending { const uint8_t* ptr; uint8_t copy[stride]; unsigned number; } pending[3];
    bool ok=true;
    auto* allocated=new(std::nothrow)NesPackedLut;
    if(!allocated)return false;
    const auto& lut=*allocated;
    // Exhaustive proof: every encoded state and every possible incoming shade
    // must remain in the set and agree with independent threshold counters.
    for(unsigned id=0;id<NesPackedLut::COUNT;++id)for(unsigned shade=0;shade<4;++shade) {
        const uint32_t entry=lut.entries[shade*NesPackedLut::COUNT+id];
        if((entry&511)>=NesPackedLut::COUNT){delete allocated;return false;}
        unsigned pushes[3];
        for(unsigned t=0;t<3;++t) {
            const unsigned old=(lut.states()[id]>>(4*t))&15;
            const unsigned value=(lut.states()[entry&511]>>(4*t))&15;
            if(value!=next(old,shade<t+1,6))ok=false;
            pushes[t]=drive(old,shade<t+1);
        }
        const unsigned expected=(pushes[1]<<6)|(pushes[1]<<4)|(pushes[0]<<2)|pushes[2];
        if(((entry>>16)&255)!=expected)ok=false;
        const unsigned opposite=(pushes[2]<<6)|(pushes[0]<<4)|(pushes[1]<<2)|pushes[1];
        if((entry>>24)!=opposite)ok=false;
    }
    for(int banked=0;banked<2;++banked)
    for(int reverse=0;reverse<2;++reverse) for(int queued=0;queued<2;++queued)
    for(int tail=0;tail<2;++tail) for(int schedule=0;schedule<3;++schedule) {
      memset(scalar,15,sizeof(scalar));memset(history,255,sizeof(history));
      for(int scan=0;scan<24;++scan) {
        const bool reset=scan==0||scan==15;
        for(int i=0;i<(int)sizeof(source);++i)source[i]=scan<7 ? 255 : scan<10 ? 0 : (i*73+scan*37)&255;
        for(int g=0;g<groups;++g) for(int repeat=0;repeat<2;++repeat) {
            uint8_t bits[8]={};
            for(int x=0;x<width;++x) {
                int shade=(source[g*pixels/4+(x/2)/4]>>(6-2*((x/2)&3)))&3;
                int dx=reverse ? 1-repeat : repeat,dy=reverse ? x&1 : 1-(x&1);
                const unsigned masks[4]={0,1,11,15};
                bool white=!(masks[shade]&(1<<(dy*2+dx)));
                if(white)bits[(x+8)/8]|=128>>((x+8)&7);
            }
            memset(expected[g*2+repeat],0,stride);
            buildRow(expected[g*2+repeat],bits,scalar+(g*2+repeat)*width,64,8,width,reset,6);
        }
        unsigned sent=0,completed=0;int count=0;
        const auto intact=[&]() { for(int i=0;i<count;++i)if(memcmp(pending[i].ptr,pending[i].copy,stride))ok=false; };
        const auto complete=[&]() {
            intact();if(!count){ok=false;return;}
            if(pending[0].number!=completed+1)ok=false;
            ++completed;for(int i=1;i<count;++i)pending[i-1]=pending[i];--count;
        };
        const auto send=[&](uint8_t* row,int y) {
            intact();if(y!=(int)sent||count>=3){ok=false;return false;}
            for(int i=0;i<stride;++i)if(row[i]!=(y>=3&&y<11 ? expected[y-3][i] : 0))ok=false;
            pending[count].ptr=row;memcpy(pending[count].copy,row,stride);pending[count++].number=++sent;
            if(schedule==1||(schedule==2&&(sent+scan)%3==0))complete();
            return true;
        };
        const auto wait=[&](unsigned goal) { intact();if(goal>sent){ok=false;return false;}while(completed<goal)complete();return true; };
        buffers[0]=0xa5;buffers[sizeof(buffers)-1]=0x5a;
        uint8_t* banks[2]={history+2*pixels*9/8,history};
        if(!queueNes(lut,source,history,buffers+1,stride,height,8,3,width,groups*2,reset,reverse,send,wait,tail,queued,banked ? banks : nullptr,2))ok=false;
        if(sent!=height+tail||completed!=sent||count||buffers[0]!=0xa5||buffers[sizeof(buffers)-1]!=0x5a)ok=false;
        // Validate every stored counter, not just the resulting drive bytes.
        for(int g=0;g<groups;++g)for(int x=0;x<pixels;++x)for(int t=0;t<3;++t) {
            const int dotx=t==1 ? 1 : 0,doty=t==2 ? 1 : 0;
            const int row=reverse ? 1-dotx : dotx,col=x*2+(reverse ? doty : 1-doty);
            const uint8_t expectedState=scalar[(g*2+row)*width+col];
            const int storedGroup=banked ? (g+2)%groups : g;
            const unsigned id=nesHistoryId(history+storedGroup*pixels*9/8,x);
            if(id>=NesPackedLut::COUNT){ok=false;continue;}
            const uint8_t value=(lut.states()[id]>>(t*4))&15;
            if(value!=expectedState)ok=false;
        }
      }
    }
    // Every failed send/wait must terminate without subsequent buffer writes.
    for(int queued=0;queued<2;++queued)for(int tail=0;tail<2;++tail)
    for(int failWait=0;failWait<2;++failWait)for(unsigned failure=0;failure<=height+tail;++failure) {
        uint8_t savedBuffers[sizeof(buffers)],savedHistory[sizeof(history)];bool stopped=false;
        const auto abort=[&]() { if(stopped)ok=false;stopped=true;memcpy(savedBuffers,buffers,sizeof(buffers));memcpy(savedHistory,history,sizeof(history));return false; };
        const auto send=[&](uint8_t*,int y){if(stopped)ok=false;return !failWait&&(unsigned)y==failure ? abort() : true;};
        const auto wait=[&](unsigned goal){if(stopped)ok=false;return failWait&&goal==failure ? abort() : true;};
        bool success=queueNes(lut,source,history,buffers+1,stride,height,8,3,width,groups*2,false,true,send,wait,tail,queued);
        if(success==stopped)ok=false;
        if(stopped&&(memcmp(savedBuffers,buffers,sizeof(buffers))||memcmp(savedHistory,history,sizeof(history))))ok=false;
    }
    delete allocated;return ok;
}
}
#undef INK_NES_SCAN_FAST
