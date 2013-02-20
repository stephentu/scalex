#include <cassert>
#include <ctime>
#include <cstring>
#include <mutex>
#include <unistd.h>

#include "rcu.hpp"
#include "macros.hpp"
#include "timer.hpp"

using namespace std;

atomic<rcu::epoch_t> rcu::global_epoch(0);
atomic<bool> rcu::gc_thread_started(false);

__thread unsigned int rcu::tl_crit_section_depth = 0;
__thread rcu::epoch_t rcu::tl_current_epoch = 0;

spinlock rcu::rcu_mutex;
aligned_padded_elem<rcu::sync> rcu::syncs[NSyncs];

void
rcu::init()
{
  // double-check-locking (DCL) pattern
  if (likely(gc_thread_started.load(memory_order_acquire)))
    return;
  lock_guard<spinlock> l(rcu_mutex);
  if (gc_thread_started.load(memory_order_acquire))
    return;
  // start gc thread as daemon thread
  thread t(gc_loop);
  t.detach(); // daemonize
  gc_thread_started.store(memory_order_release);
}

void
rcu::region_begin()
{
  if (!tl_crit_section_depth++) {
    sync &s = sync_for_thread();
    s.local_critical_mutex.lock();
    tl_current_epoch = global_epoch.load(memory_order_acquire);
  }
}

void
rcu::region_end()
{
  assert(tl_crit_section_depth);
  if (!--tl_crit_section_depth) {
    sync &s = sync_for_thread();
    s.local_critical_mutex.unlock();
  }
}

void
rcu::free_with_fn(void *p, deleter_t fn)
{
  init(); // make sure RCU GC loop is running
  assert(tl_crit_section_depth);
  sync &s = sync_for_thread();
  s.local_queues[tl_current_epoch % 2].push_back(move(delete_entry(p, fn)));
}

static const uint64_t rcu_epoch_us = 50 * 1000; /* 50 ms */

void
rcu::gc_loop()
{
  struct timespec t;
  memset(&t, 0, sizeof(t));
  timer loop_timer;
  // runs as daemon thread
  for (;;) {
    const uint64_t last_loop_usec = loop_timer.lap();
    const uint64_t delay_time_usec = rcu_epoch_us;
    if (last_loop_usec < delay_time_usec) {
      t.tv_nsec = (delay_time_usec - last_loop_usec) * 1000;
      nanosleep(&t, NULL);
    }

    // increment global epoch
    const epoch_t cleaning_epoch = global_epoch.load(memory_order_acquire);
    global_epoch.store(cleaning_epoch + 1); // sequentially consistent store

    delete_queue elems;

    // now wait for each thread to finish any outstanding critical sections
    // from the previous epoch, and advance it forward to the global epoch
    for (size_t i = 0; i < NSyncs; i++) {
      sync &s = syncs[i].elem;

      {
        lock_guard<spinlock> l(s.local_critical_mutex);
      }

      // now the next time the thread enters a critical section, it
      // *must* get the new global_epoch, so we can now claim its
      // deleted pointers from global_epoch - 1
      delete_queue &q = s.local_queues[cleaning_epoch % 2];
      elems.insert(elems.end(), q.begin(), q.end());
      q.clear();
    }

    for (delete_queue::iterator it = elems.begin();
         it != elems.end(); ++it)
      it->second(it->first);
    elems.clear();
  }
}
