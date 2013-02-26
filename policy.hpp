#pragma once

#include "linked_list.hpp"
#include "global_lock_impl.hpp"
#include "per_node_lock_impl.hpp"
#include "lock_free_impl.hpp"

#include "rcu.hpp"
#include "atomic_reference.hpp"

template <typename T>
struct ll_policy {
  typedef global_lock_impl<T> global_lock;
  typedef per_node_lock_impl<T> per_node_lock;
  typedef lock_free_impl<T> lock_free;
  typedef lock_free_impl<T, nop_lock, nop_ref_counted, scoped_rcu_region>
          lock_free_rcu;
};
