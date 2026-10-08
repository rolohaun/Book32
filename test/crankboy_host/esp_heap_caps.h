#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
extern int allocationBudget;
static inline void* heap_caps_calloc(size_t n, size_t bytes, int flags) {
    (void)flags;
    if (allocationBudget == 0) return NULL;
    if (allocationBudget > 0) --allocationBudget;
    return calloc(n, bytes);
}
