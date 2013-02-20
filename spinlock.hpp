#pragma once

#include <atomic>
#include "asm.hpp"

// implements BasicLockable concept (C++11)
class spinlock {
public:
  spinlock() : flag_(false) {}

  // non-copyable/non-movable
  spinlock(const spinlock &) = delete;
  spinlock(spinlock &&) = delete;
  spinlock &operator=(const spinlock &) = delete;

  inline void
  lock()
  {
    while (flag_.exchange(true, std::memory_order_acquire))
      nop_pause();
  }

  inline void
  unlock()
  {
    flag_.store(false, std::memory_order_release);
  }

private:
  std::atomic<bool> flag_;
};
