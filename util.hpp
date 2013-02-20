#pragma once

#include "macros.hpp"

// padded, aligned primitives
template <typename T>
struct aligned_padded_elem {
  aligned_padded_elem() : elem() {}
  T elem;
  CACHE_PADOUT;
} CACHE_ALIGNED;
