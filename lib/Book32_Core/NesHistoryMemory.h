#pragma once
#include <stddef.h>
#include <stdint.h>

namespace NesHistoryMemory {
constexpr unsigned BANKS=32;
constexpr unsigned BYTES=256*240*9/8;
constexpr unsigned BANK_BYTES=BYTES/BANKS;
constexpr unsigned RESERVE=20*1024;
constexpr unsigned ALLOCATOR_MARGIN=64;

// Small independently placed banks tolerate fragmentation. Keep every useful
// internal bank instead of discarding all of them when just one needs PSRAM.
// A failed complete allocation owns nothing; the caller can safely fall back.
template<typename FreeBytes,typename Internal,typename External,typename Release>
bool allocate(uint8_t* (&banks)[BANKS],FreeBytes freeBytes,Internal internal,
              External external,Release release) {
    for(auto bank:banks)if(bank)return false;
    for(auto& bank:banks) {
        if(freeBytes()>=RESERVE+BANK_BYTES+ALLOCATOR_MARGIN) {
            bank=internal(BANK_BYTES);
            // Also check after allocation: heap metadata sizes can vary.
            if(bank && freeBytes()<RESERVE) { release(bank);bank=nullptr; }
        }
        if(!bank)bank=external(BANK_BYTES);
        if(!bank) {
            for(auto& owned:banks) { if(owned)release(owned);owned=nullptr; }
            return false;
        }
    }
    return true;
}
}
