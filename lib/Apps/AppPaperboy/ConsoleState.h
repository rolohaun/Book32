#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
// Fixed-size, checksummed per-core envelope. No process pointers are stored.
#define INK_CONSOLE_IMAGE_BYTES (160U*144U)
typedef struct { uint32_t magic, version, system, romCrc, romBytes, used, payloadCrc, reserved; } InkConsoleHeader;
static inline uint32_t inkConsoleCrc(const void* ptr,size_t bytes) {
    static const uint32_t lut[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const uint8_t* p=(const uint8_t*)ptr; uint32_t crc=~0U;
    while(bytes--) { crc^=*p++; crc=(crc>>4)^lut[crc&15]; crc=(crc>>4)^lut[crc&15]; }
    return ~crc;
}
static inline void inkConsoleSeal(void* data,size_t size,unsigned system,uint32_t romCrc,size_t romBytes,size_t used) {
    InkConsoleHeader h={0x534b4e49,1,system,romCrc,(uint32_t)romBytes,(uint32_t)used,0,0};
    h.payloadCrc=inkConsoleCrc((uint8_t*)data+sizeof(h),size-sizeof(h)); memcpy(data,&h,sizeof(h));
}
static inline bool inkConsoleCheck(const void* data,size_t size,unsigned system,uint32_t romCrc,size_t romBytes,size_t capacity) {
    if (!data || size!=sizeof(InkConsoleHeader)+INK_CONSOLE_IMAGE_BYTES+capacity) return false;
    InkConsoleHeader h; memcpy(&h,data,sizeof(h));
    return h.magic==0x534b4e49 && h.version==1 && h.system==system && h.romCrc==romCrc && h.romBytes==romBytes &&
        h.reserved==0 && h.used>0 && h.used<=capacity && h.payloadCrc==inkConsoleCrc((const uint8_t*)data+sizeof(h),size-sizeof(h));
}
static inline uint8_t inkGray565(uint16_t color) {
    unsigned r=(color>>11)*255/31,g=((color>>5)&63)*255/63,b=(color&31)*255/31;
    return 3-((r*77+g*150+b*29)>>14);
}
