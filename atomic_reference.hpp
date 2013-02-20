#pragma once

#include <cassert>
#include <atomic>

#include "asm.hpp"

/**
 * A std::shared_ptr<T>-like abstraction for reference counting,
 * except we guarantee atomicity. While C++11 provides atomic_*
 * specializations of functions which operate on shared_ptr objects,
 * they are not quite desirable yet:
 *
 * A) As of gcc 4.7, an atomic implementation is not yet provided in libstdc++
 *
 * B) There's no builtin support in shared_ptrs for marked ptrs. Working around
 *    it with custom deleters is a bit messy. Easier to build an abstraction
 *    which understands marked pointers
 *
 * C) A custom implementation is more efficient memory-wise, because we can force
 *    ref counted pointers to implement a common interface (and save having to
 *    allocated a separate control block)
 */

class atomic_ref_counted {
protected:
  // construction does NOT increment reference count
  atomic_ref_counted() : count_(0) {}
  ~atomic_ref_counted()
  {
    assert(count_.load() == 0);
  }

public:
  inline void
  inc()
  {
    count_++;
  }

  // returns true if last decrement
  inline bool
  dec()
  {
    assert(count_.load() > 0);
    return --count_ == 0;
  }

private:
  std::atomic<uint32_t> count_;
};

class nop_ref_counted {
public:
  inline void inc() {}
  inline bool dec() { return false; }
};

namespace private_ {

template <typename T>
struct ptr_ops_mixin {

  typedef intptr_t opaque_t;

  static inline opaque_t
  Mark(opaque_t p)
  {
    return p | 0x1;
  }

  static inline bool
  IsMarked(opaque_t p)
  {
    return p & 0x1;
  }

  static inline T *
  Ptr(opaque_t p)
  {
    return (T *) (p & ~0x1);
  }

  static inline opaque_t
  BuildOpaque(T *ptr, opaque_t op)
  {
    return opaque_t(ptr) | (op & 0x1);
  }
};

}

// T must inherit atomic_ref_counted (or implement the same interface)
// this class also supports one-time marking of ptrs.
//
// Doesn't support custom deleter
template <typename T>
class atomic_ref_ptr : public private_::ptr_ops_mixin<T> {
  typedef typename private_::ptr_ops_mixin<T>::opaque_t opaque_t;

public:
  // nullptr constructor
  atomic_ref_ptr() : ptr_(opaque_t(nullptr)), mutex_() {}

  ~atomic_ref_ptr() {
    T *ptr = get();
    if (ptr && ptr->dec())
      delete ptr;
  }

  // constructors don't accept a marked ptr
  explicit atomic_ref_ptr(T *ptr)
    : ptr_(opaque_t(ptr)), mutex_()
  {
    if (ptr)
      ptr->inc();
  }

  template <typename U>
  explicit atomic_ref_ptr(U *ptr)
    : ptr_(opaque_t(static_cast<T *>(ptr))), mutex_()
  {
    if (ptr)
      ptr->inc();
  }

  // Copy construction/assignment
  //
  // NOTE: Copy assignments don't propagate the marks
  //
  // NOTE: Assigning to a reference preserves its current mark

  atomic_ref_ptr(const atomic_ref_ptr &other)
    : ptr_(opaque_t(nullptr)), mutex_()
  {
    assignFrom(other);
  }

  template <typename U>
  atomic_ref_ptr(const atomic_ref_ptr<U> &other)
    : ptr_(opaque_t(other.get())), mutex_()
  {
    assignFrom(other);
  }

  atomic_ref_ptr &
  operator=(const atomic_ref_ptr &other)
  {
    assignFrom(other);
    return *this;
  }

  template <typename U>
  atomic_ref_ptr &
  operator=(const atomic_ref_ptr<U> &other)
  {
    assignFrom(other);
    return *this;
  }

  explicit inline
  operator bool() const
  {
    return get();
  }

  T &
  operator*() const
  {
    return *get();
  }

  T *
  operator->() const
  {
    return get();
  }

  template <typename U>
  inline bool
  operator==(const atomic_ref_ptr<U> &other) const
  {
    return get() == other.get();
  }

  template <typename U>
  inline bool
  operator!=(const atomic_ref_ptr<U> &other) const
  {
    return !operator==(other);
  }

  inline T *
  get() const
  {
    return this->Ptr(get_raw());
  }

  inline bool
  get_mark() const
  {
    return this->IsMarked(get_raw());
  }

  // returns when this ptr is marked- returns
  // true if the caller was the one responsible for the marking
  inline bool
  mark()
  {
  retry:
    opaque_t this_opaque = get_raw();
    if (this->IsMarked(this_opaque))
      return false;
    opaque_t new_opaque = this->Mark(this_opaque);
    if (!ptr_.compare_exchange_strong(this_opaque, new_opaque)) {
      nop_pause();
      goto retry;
    }
    assert(get_mark());
    return true;
  }

  // desired_value is stable by default because it is pass by value, so we
  // don't need to lock it
  inline bool
  compare_exchange_strong(
      const atomic_ref_ptr &expected_value,
      atomic_ref_ptr desired_value)
  {
    std::lock(mutex_, expected_value.mutex_);
    std::lock_guard<spinlock> l0(mutex_, std::adopt_lock);
    std::lock_guard<spinlock> l1(expected_value.mutex_, std::adopt_lock);
    opaque_t expected_opaque = expected_value.ptr_.load(); // assume stable
    opaque_t desired_opaque = desired_value.ptr_.load();
    if (!ptr_.compare_exchange_strong(expected_opaque, desired_opaque))
      return false;
    T *expected_ptr = this->Ptr(expected_opaque);
    T *desired_ptr = this->Ptr(desired_opaque);
    if (expected_ptr == desired_ptr)
      // self-exchange
      return true;
    if (desired_ptr)
      desired_ptr->inc();
    if (expected_ptr && expected_ptr->dec())
      delete expected_ptr;
    return true;
  }

private:

  template <typename U>
  void
  assignFrom(const atomic_ref_ptr<U> &other)
  {
  retry:
    std::lock(mutex_, other.mutex_);
    std::lock_guard<spinlock> l0(mutex_, std::adopt_lock);
    std::lock_guard<spinlock> l1(other.mutex_, std::adopt_lock);

    opaque_t this_opaque = get_raw();
    T *this_ptr = this->Ptr(this_opaque);
    T *that_ptr = other.get();
    if (this_ptr == that_ptr) {
      // self-assignment
      return;
    }
    opaque_t new_opaque = this->BuildOpaque(that_ptr, this_opaque);
    // could have a concurrent marker
    if (!ptr_.compare_exchange_strong(this_opaque, new_opaque)) {
      nop_pause();
      goto retry;
    }
    if (that_ptr)
      that_ptr->inc();
    if (this_ptr && this_ptr->dec())
      delete this_ptr;
  }

  inline opaque_t
  get_raw() const
  {
    return ptr_.load();
  }

  std::atomic<opaque_t> ptr_;

  // this spinlock guards Ptr(ptr_) from changing (marks can change w/o grabing
  // mutex)
  //
  // Why do we need a mutex for ref counting? This is because we assume
  // that the source of a copy assignment (ie v in p = v) is un-stable,
  // that is, it can experience concurrent modification during the assignment.
  // Note that without this assumption, this pointer is of limited use.
  //
  // Given this assumption, each assignment has a potental race condition!
  // That is, it is not possible to do a load() from the source followed by
  // an increment of the reference count *atomically* w/o a lock. Thus, we
  // need a lock to allow us to atomically load and increment.
  mutable spinlock mutex_;
};
