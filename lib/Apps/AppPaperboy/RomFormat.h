#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
typedef enum { INK_SYSTEM_NONE=0, INK_SYSTEM_GB=1, INK_SYSTEM_NES=2, INK_SYSTEM_SEGA=3 } InkSystem;
static inline bool inkExtension(const char* ext, const char* expected) {
    if (!ext) return false;
    while (*ext && *expected) {
        char c=*ext++; if (c>='A' && c<='Z') c+=32;
        if (c!=*expected++) return false;
    }
    return !*ext && !*expected;
}
static inline InkSystem inkRomSystem(const char* name) {
    const char* ext=name ? strrchr(name,'.') : NULL;
    if (inkExtension(ext,".gb") || inkExtension(ext,".gbc")) return INK_SYSTEM_GB;
    if (inkExtension(ext,".nes")) return INK_SYSTEM_NES;
    if (inkExtension(ext,".md") || inkExtension(ext,".gen") || inkExtension(ext,".bin")) return INK_SYSTEM_SEGA;
    return INK_SYSTEM_NONE;
}
static inline const char* inkSystemName(InkSystem system) {
    return system==INK_SYSTEM_GB ? "Game Boy" : system==INK_SYSTEM_NES ? "NES" : system==INK_SYSTEM_SEGA ? "Sega" : "Unknown";
}
static inline size_t inkRomLimit(InkSystem system) {
    return system==INK_SYSTEM_GB ? 8U*1024U*1024U : system==INK_SYSTEM_NES ? 2U*1024U*1024U : 4U*1024U*1024U;
}
static inline bool inkRomSizeValid(InkSystem system, size_t bytes) {
    size_t minimum=system==INK_SYSTEM_GB ? 32768 : system==INK_SYSTEM_NES ? 16400 : 512;
    return system!=INK_SYSTEM_NONE && bytes>=minimum && bytes<=inkRomLimit(system);
}
// Large cartridges use pinned SD banks instead of exhausting 8 MB PSRAM.
#define INK_ROM_MAX_BYTES (8U * 1024U * 1024U)
static inline const char* inkRomValidate(const uint8_t* header, size_t bytes) {
    if (!header || bytes < 32768 || bytes > INK_ROM_MAX_BYTES) return "ROM must be 32 KB to 8 MB";
    if (header[0x148] > 8 || bytes != (32768U << header[0x148])) return "ROM size does not match its header";
    if (header[0x149] > 5) return "Invalid cartridge RAM size";
    uint8_t sum = 0;
    for (unsigned i=0x134; i<=0x14c; ++i) sum = sum-header[i]-1;
    if (sum != header[0x14d]) return "Invalid ROM header checksum";
    return NULL;
}
static inline bool inkRomFilename(const char* name) {
    size_t n = name ? strlen(name) : 0;
    if (n < 4 || n > 96 || name[0] == '.' || name[n-1] == ' ') return false;
    for (size_t i=0; i<n; ++i) {
        unsigned char c = name[i];
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) return false;
    }
    return inkRomSystem(name)!=INK_SYSTEM_NONE;
}
// Caller supplies at least min(file size, 512) header bytes.
static inline const char* inkRomValidateSystem(InkSystem system, const uint8_t* h, size_t bytes) {
    if (!h || !inkRomSizeValid(system,bytes)) return "Invalid ROM size for this system";
    if (system==INK_SYSTEM_GB) return inkRomValidate(h,bytes);
    if (system==INK_SYSTEM_NES) {
        if (memcmp(h,"NES\x1a",4)) return "Not an iNES ROM";
        if ((h[7]&12)==8) return "NES 2.0 is not supported; use an iNES ROM";
        if (h[7]&3) return "VS/PlayChoice ROMs are not supported";
        size_t expected=16U+((h[6]&4)?512U:0U)+h[4]*16384U+h[5]*8192U;
        if (!h[4] || bytes!=expected) return "NES size does not match its header";
        return NULL;
    }
    if ((bytes&1) || memcmp(h+0x100,"SEGA",4)) return "Use a raw Genesis .md/.gen/.bin ROM (not SMD or ZIP)";
    uint32_t reset=((uint32_t)h[4]<<24)|((uint32_t)h[5]<<16)|((uint32_t)h[6]<<8)|h[7];
    if ((reset&1) || reset>=bytes) return "Invalid Genesis reset vector";
    return NULL;
}
