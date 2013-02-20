#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>

#include "spinlock.hpp"
#include "util.hpp"

class rcu {
public:
  typedef uint64_t epoch_t;

  typedef void (*deleter_t)(void *);
  typedef std::pair<void *, deleter_t> delete_entry;
  typedef std::vector<delete_entry> delete_queue;

  template <typename T>
  static inline void
  deleter(void *p)
  {
    delete (T *) p;
  }

  template <typename T>
  static inline void
  deleter_array(void *p)
  {
    delete [] (T *) p;
  }

  // all threads interact w/ the RCU subsystem via
  // a sync struct
  struct sync {
    sync() = default;
    sync(const sync &) = delete;
    sync &operator=(const sync &) = delete;
    delete_queue local_queues[2];
    spinlock local_critical_mutex;
  };

  static void region_begin();
  static void region_end();

  static void free_with_fn(void *p, deleter_t fn);

  template <typename T>
  static inline void
  free(T *p)
  {
    free_with_fn(p, deleter<T>);
  }

  template <typename T>
  static inline void
  free_array(T *p)
  {
    free_with_fn(p, deleter_array<T>);
  }

private:
  static void init();

  static void gc_loop();

  static inline sync&
  sync_for_thread()
  {
    std::thread::id this_id = std::this_thread::get_id();
    const size_t h = std::hash<std::thread::id>()(this_id);
    return syncs[h % NSyncs].elem;
  }

  static spinlock rcu_mutex; // protects init()

  static std::atomic<epoch_t> global_epoch;

  static std::atomic<bool> gc_thread_started; // init() is idempotent

  // allows recursive RCU regions
  static __thread unsigned int tl_crit_section_depth;
  static __thread epoch_t tl_current_epoch;

  static const size_t NSyncs = 1024;
  static aligned_padded_elem<sync> syncs[NSyncs];
};

class scoped_rcu_region {
public:
  inline scoped_rcu_region()
  {
    rcu::region_begin();
  }

  inline ~scoped_rcu_region()
  {
    rcu::region_end();
  }

  template <typename T>
  inline void
  release(T *p) const
  {
    rcu::free_with_fn(p, rcu::deleter<T>);
  }
};
