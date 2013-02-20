#pragma once

#include <cstdint>
#include <sys/time.h>

class timer {
public:
  inline timer()
  {
    lap();
  }

  timer(const timer &) = delete;
  timer &operator=(const timer &) = delete;

  inline uint64_t
  lap()
  {
    const uint64_t t0 = start;
    const uint64_t t1 = cur_usec();
    start = t1;
    return t1 - t0;
  }

private:
  static inline uint64_t
  cur_usec()
  {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
  }

  uint64_t start;
};
