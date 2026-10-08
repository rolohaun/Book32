#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include <vector>
inline void* ps_malloc(size_t bytes) { return malloc(bytes); }
#include "../lib/Book32_Core/GameVideoSelfTest.h"
int main() {
    // Fragmented heaps, variable allocator overhead, reserve enforcement and
    // rollback on every external-bank failure. Partial SRAM use is intentional.
    {
        using namespace NesHistoryMemory;
        struct Allocation { uint8_t* pointer;size_t charge; };
        for(size_t budget : {size_t(18000),size_t(50000),size_t(90000),size_t(120000)})
        for(size_t largest : {size_t(1024),size_t(4096),size_t(16000)})
        for(size_t overhead : {size_t(16),size_t(512)}) {
            size_t available=budget;unsigned calls=0;
            uint8_t* banks[BANKS]={};std::vector<Allocation> allocations;
            const auto release=[&](uint8_t* p) {
                bool found=false;
                for(auto& a:allocations)if(a.pointer==p) {
                    available+=a.charge;free(p);a.pointer=nullptr;found=true;break;
                }
                assert(found);
            };
            bool ok=allocate(banks,[&](){return available;},[&](size_t bytes)->uint8_t* {
                if(++calls==3 || bytes>largest || available<bytes+overhead)return nullptr;
                auto* p=(uint8_t*)malloc(bytes);assert(p);available-=bytes+overhead;
                allocations.push_back({p,bytes+overhead});return p;
            },[&](size_t bytes) {
                auto* p=(uint8_t*)malloc(bytes);assert(p);allocations.push_back({p,0});return p;
            },release);
            assert(ok && available>=(budget<RESERVE ? budget : RESERVE));
            for(auto& p:banks) { assert(p);release(p);p=nullptr; }
            assert(available==budget);
            for(auto a:allocations)assert(!a.pointer);
        }
        for(unsigned failure=0;failure<BANKS;++failure) {
            uint8_t* banks[BANKS]={};unsigned made=0,freed=0;
            bool ok=allocate(banks,[](){return 0U;},[](size_t)->uint8_t* {assert(false);return nullptr;},
                [&](size_t bytes)->uint8_t* {if(made==failure)return nullptr;++made;return (uint8_t*)malloc(bytes);},
                [&](uint8_t* p){assert(p);++freed;free(p);});
            assert(!ok && made==freed);
            for(auto p:banks)assert(!p);
        }
    }
    assert(gameVideoSelfTest());
    // Actual 32-bank geometry, with physically reordered banks, agrees with
    // contiguous storage for every DMA byte and history bit. Also exercise
    // the sampled profiler specialization used on the device.
    {
        using namespace InkDeckVideoPulse;
        using namespace NesHistoryMemory;
        struct Profile {
            unsigned* counts;
            unsigned begin() const { return 0; }
            void end(NesScanStage stage,unsigned) const { ++counts[unsigned(stage)]; }
        };
        auto* lut=new NesPackedLut;
        std::vector<uint8_t> image(15360),flat(BYTES),banked(BYTES),reference(540*256),dma(5*256);
        uint8_t* banks[BANKS];
        for(unsigned i=0;i<BANKS;++i)banks[i]=banked.data()+((i*13)%BANKS)*BANK_BYTES;
        for(bool reverse : {false,true})for(unsigned scan=0;scan<10;++scan) {
            for(unsigned i=0;i<image.size();++i)image[i]=(i*71+scan*29)&255;
            auto previous=flat;
            unsigned sent=0;
            const auto wait=[&](unsigned goal){return goal<=sent;};
            const auto save=[&](uint8_t* row,int y){assert(y==int(sent++));memcpy(reference.data()+y*256,row,256);return true;};
            assert(queueNes(*lut,image.data(),flat.data(),dma.data(),256,540,56,14,480,512,scan==0,reverse,save,wait));
            assert(sent==540);
            for(int placement : {0,1,2}) {
                sent=0;unsigned counts[4]={};
                for(unsigned i=0;i<BANKS;++i)memcpy(banks[i],previous.data()+i*BANK_BYTES,BANK_BYTES);
                const auto compare=[&](uint8_t* row,int y){assert(y==int(sent++));assert(!memcmp(reference.data()+y*256,row,256));return true;};
                const auto resident=[&](const uint8_t* state) {
                    return placement==1 || (placement==2 && ((state-banked.data())/BANK_BYTES)%2);
                };
                assert(queueNes(*lut,image.data(),nullptr,dma.data(),256,540,56,14,480,512,scan==0,reverse,
                    compare,wait,false,true,banks,8,Profile{counts},resident));
                assert(sent==540 && counts[0]==512 && counts[1]==256 && counts[2]==540 && counts[3]>0);
                for(unsigned i=0;i<BANKS;++i)assert(!memcmp(flat.data()+i*BANK_BYTES,banks[i],BANK_BYTES));
            }
        }
        delete lut;
    }
    // Block-neutral shortcuts must preserve every waveform state, including
    // high-bit aliases and unfinished transitions. Exercise unaligned banks
    // and both orientations against the independent physical-dot reference.
    {
        using namespace InkDeckVideoPulse;
        auto* lut=new NesPackedLut;
        for(unsigned shade=0;shade<4;++shade)assert(lut->reset[shade]<256);
        uint8_t source[2],storage[11],dots[32],first[4],second[4],reference[2][4];
        uint8_t* history=storage+1;
        for(unsigned colors=0;colors<256;++colors)for(bool reverse : {false,true}) {
            source[0]=colors;source[1]=colors^0x63;
            for(int variation=0;variation<10;++variation) {
                storage[0]=0xab;storage[10]=0xba;history[8]=0;
                for(unsigned p=0;p<8;++p) {
                    unsigned shade=(source[p/4]>>(6-2*(p%4)))&3;
                    unsigned id=lut->reset[shade];
                    if(variation<8 && p==unsigned(variation))id=NesPackedLut::COUNT-1;
                    // Same low byte as a settled ID, but different high bit.
                    if(variation==8 && p==3)id=256+id;
                    history[p]=id;history[8]|=(id>>8)<<p;
                    unsigned state=lut->states()[id];
                    for(int row=0;row<2;++row)for(int col=0;col<2;++col) {
                        const int dx=reverse ? 1-row : row,dy=reverse ? col : 1-col;
                        unsigned t=dx==1 ? 1 : dy==1 ? 2 : 0;
                        dots[row*16+p*2+col]=(state>>(4*t))&15;
                    }
                }
                for(int row=0;row<2;++row) {
                    uint8_t bits[2]={};
                    for(int x=0;x<16;++x) {
                        unsigned shade=(source[(x/2)/4]>>(6-2*((x/2)%4)))&3;
                        int dx=reverse ? 1-row : row,dy=reverse ? x%2 : 1-x%2;
                        if(!NesViewport::black(shade,dx,dy))bits[x/8]|=128>>(x%8);
                    }
                    buildRow(reference[row],bits,dots+row*16,16,0,16,false,6);
                }
                buildNesRows(*lut,first,second,source,history,8,false,reverse);
                assert(!memcmp(first,reference[0],4)&&!memcmp(second,reference[1],4));
                for(unsigned p=0;p<8;++p)for(int t=0;t<3;++t) {
                    int dx=t==1 ? 1 : 0,dy=t==2 ? 1 : 0;
                    int row=reverse ? 1-dx : dx,col=p*2+(reverse ? dy : 1-dy);
                    unsigned id=nesHistoryId(history,p);assert(id<NesPackedLut::COUNT);
                    assert(((lut->states()[id]>>(4*t))&15)==dots[row*16+col]);
                }
                assert(storage[0]==0xab && storage[10]==0xba);
            }
        }
        delete lut;
    }
    // Fused palette conversion must match the full native-image path exactly,
    // including PPU padding, every gray shade, both rotations and byte guards.
    {
        uint8_t indexed[272*240],native[256*240],palette[256];
        uint8_t compact[NesViewport::PACKED_BYTES+2],reference[NesViewport::PACKED_BYTES];
        for(int i=0;i<256;i++)palette[i]=(i*7+i/4)&3;
        memset(indexed,0xce,sizeof(indexed));
        for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
            const uint8_t value=(x*13+y*17)&255;
            indexed[y*272+8+x]=value;native[y*256+x]=palette[value];
        }
        for(uint8_t r : {1,3}) {
            compact[0]=0x65;compact[sizeof(compact)-1]=0x56;
            NesViewport::packIndexed(compact+1,indexed+8,272,palette,r);
            NesViewport::pack2x(reference,native,r);
            assert(!memcmp(compact+1,reference,sizeof(reference)));
            assert(compact[0]==0x65 && compact[sizeof(compact)-1]==0x56);
        }
    }
    // Full-frame copy leaves all eight border pixels white, both sides.
    using namespace LilygoLayout;
    uint8_t canvas[PITCH*CANVAS_H], panel[PANEL_W*PANEL_H/8];
    memset(canvas,0,sizeof(canvas)); memset(panel,255,sizeof(panel));
    for(int y=0;y<CANVAS_H;++y) memcpy(panel+(y+INSET)*120+INSET/8,canvas+y*PITCH,PITCH);
    for(int y=0;y<PANEL_H;++y) for(int x=0;x<PANEL_W;++x) {
        bool margin = x<INSET || x>=PANEL_W-INSET || y<INSET || y>=PANEL_H-INSET;
        assert(!!(panel[y*120+x/8] & (128>>(x%8))) == margin);
    }
    puts("PASS: safe canvas margins, both game rotations, clipping, DMA and frame timing");
}
