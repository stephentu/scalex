#pragma once

// These macros assume GCC

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define CACHELINE_SIZE 64 // XXX: don't assume x86

#define CACHE_ALIGNED __attribute__((aligned(CACHELINE_SIZE)))
#define CACHE_PADOUT  char __padout[0] __attribute__((aligned(CACHELINE_SIZE)))
#define PACKED __attribute__((packed))

#define UNUSED __attribute__((unused))
