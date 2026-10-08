#pragma once
// WASI test-only boundary: positive-path tests never call longjmp. Expected
// allocation/SD-read exceptions are covered separately by the native suite.
typedef int jmp_buf[1];
#define setjmp(env) 0
static inline void longjmp(jmp_buf env, int value) { (void)env; (void)value; __builtin_trap(); }
