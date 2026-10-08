#pragma once
#define CC_USE_C99_INTEGERS
#include "clownmdemu/source/clownmdemu.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// InkDeck-local adaptations; no restricted Gwenesis/Musashi/Z80 code remains.
bool inkGenesisLookupAllocate(void* (*allocate)(size_t));
void inkGenesisLookupFree(void);
void inkGenesisSilentFm(FM* fm, size_t frames);
#ifndef ESP_PLATFORM
ClownMDEmu* inkGenesisTestCore(void);
#endif
