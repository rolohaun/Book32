/* Modified for InkDeck, 2026-10-08; see THIRD_PARTY_NOTICES.md and tools/import_nofrendo.py. */
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>
// Only included by upstream state.c: no global stdio replacement.
typedef struct { unsigned char* data; size_t capacity, position, length; int failed; } InkNesStream;
extern InkNesStream inkNesStream;
static inline InkNesStream* inkNesOpen(const char* name, const char* mode) {
    (void)name; inkNesStream.position=0; inkNesStream.failed=0;
    if (*mode=='w') inkNesStream.length=0;
    return &inkNesStream;
}
static inline size_t inkNesRead(void* out, size_t size, size_t count, InkNesStream* s) {
    if (!size || count > (s->length-s->position)/size) { s->failed=1; return 0; }
    memcpy(out,s->data+s->position,size*count); s->position+=size*count; return count;
}
static inline size_t inkNesWrite(const void* in, size_t size, size_t count, InkNesStream* s) {
    if (!size || count > (s->capacity-s->position)/size) { s->failed=1; return 0; }
    memcpy(s->data+s->position,in,size*count); s->position+=size*count;
    if (s->length<s->position) s->length=s->position;
    return count;
}
static inline int inkNesSeek(InkNesStream* s, long offset, int origin) {
    if (origin != SEEK_SET || offset<0 || (size_t)offset>s->length) { s->failed=1; return -1; }
    s->position=(size_t)offset; return 0;
}
#define FILE InkNesStream
#define fopen inkNesOpen
#define fread inkNesRead
#define fwrite inkNesWrite
#define fseek inkNesSeek
#define fclose(s) ((s)->failed ? -1 : 0)
