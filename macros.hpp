#pragma once

#include <cassert>

// These macros assume GCC

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define CACHELINE_SIZE 64 // XXX: don't assume x86

#define CACHE_ALIGNED __attribute__((aligned(CACHELINE_SIZE)))
#define CACHE_PADOUT  char __padout[0] __attribute__((aligned(CACHELINE_SIZE)))
#define PACKED __attribute__((packed))

#define UNUSED __attribute__((unused))

#ifdef NDEBUG
  #define ASSERT(expr) (likely(expr) ? (void)0 : abort())
#else
  #define ASSERT(expr) assert(expr)
#endif /* NDEBUG */

// Test for GCC >= 4.7.0
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#define OVERRIDE override
#else
#define OVERRIDE
#endif
