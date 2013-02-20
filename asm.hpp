#pragma once

static inline void
nop_pause()
{
  __asm__ volatile ("pause" ::);
}
