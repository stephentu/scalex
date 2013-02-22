#pragma once

#include <cassert>
#include <memory>
#include <iterator>

// toggle between spinlock implementation or std::mutex
#define USE_SPINLOCK

#ifdef USE_SPINLOCK
#include "spinlock.hpp"
#else
#include <mutex>
#endif

/**
 * Standard singly-linked list with a per-node locks for protection, and
 * standard reference counting. Hand over hand locking is used to
 * traverse/mutate the list.
 *
 * References returned by this implementation are guaranteed to be valid until
 * the element is removed from the list
 */
template <typename T>
class per_node_lock_impl {
private:

  struct node;

#ifdef USE_SPINLOCK
  typedef spinlock lock_type;
#else
  typedef std::mutex lock_type;
#endif

  typedef std::unique_lock<lock_type> unique_lock;
  typedef std::shared_ptr<unique_lock> unique_lock_ptr;
  typedef std::shared_ptr<node> node_ptr;

  struct node {
    // non-copyable
    node(const node &) = delete;
    node(node &&) = delete;
    node &operator=(const node &) = delete;

    node() : value_(), next_() {}
    node(const T &value, const node_ptr &next)
      : value_(value), next_(next) {}

    // Note: mutex_ must be held in order to access next_
    mutable lock_type mutex_;
    T value_;
    node_ptr next_;
  };

  // NB: multiple threads mutating the same shared_ptr instance
  // is subject to data races- however, since head_ is not mutated
  // (only read) by multiple threads, we can access it w/o a lock
  node_ptr head_; // head_ points to a sentinel beginning node

  // we do, however, need a mutex to guard the tail_ shared_ptr instance
  lock_type tail_ptr_mutex_;
  node_ptr tail_;

  struct iterator_ : public std::iterator<std::forward_iterator_tag, T> {
    iterator_() : lock_(), node_() {}
    iterator_(const unique_lock_ptr &lock, const node_ptr &node)
      : lock_(lock), node_(node) {}

    T &
    operator*() const
    {
      return node_->value_;
    }

    T *
    operator->() const
    {
      return &node_->value_;
    }

    bool
    operator==(const iterator_ &o) const
    {
      return node_ == o.node_;
    }

    bool
    operator!=(const iterator_ &o) const
    {
      return !operator==(o);
    }

    iterator_ &
    operator++()
    {
      if (node_->next_) {
        unique_lock_ptr l(new unique_lock(node_->next_->mutex_));
        node_ = node_->next_;
        lock_ = l;
      } else {
        node_.reset();
        lock_.reset();
      }
      return *this;
    }

    iterator_
    operator++(int)
    {
      iterator_ cur = *this;
      ++(*this);
      return cur;
    }

    unique_lock_ptr lock_;
    node_ptr node_;
  };

public:

  typedef iterator_ iterator;

  per_node_lock_impl() : head_(new node), tail_(head_) {}

  size_t
  size() const
  {
    size_t ret = 0;
    node_ptr prev = head_;
    head_->mutex_.lock();
    node_ptr cur = head_->next_;
    while (cur) {
      // NB: hand-over-hand locking ensures that cur doesn't become a deleted
      // object (otherwise, if we released prev's lock before acquiring cur's
      // lock, cur could be deleted by another thread).
      //
      // This, however, isn't necessarily a bad thing: by creating a node_ptr
      // pointing to cur, we do ensure that cur won't be deleted, even if we
      // were to release the lock on prev. Seeing as how size() is not a
      // linearizable operation regardless of whether or not we do
      // hand-over-hand locking (it's really only an approximating if there are
      // concurrent mutations), then it might be OK to include deleted elements
      // in the size count.
      cur->mutex_.lock();
      prev->mutex_.unlock();
      ret++;
      prev = cur;
      cur = cur->next_;
    }
    prev->mutex_.unlock();
    return ret;
  }

  inline T &
  front()
  {
    // NB: holding onto head_->mutex_ is enough to ensure that first is not
    // deleted concurrently (the ref-count is also enough)
    unique_lock l(head_->mutex_);
    node_ptr first = head_->next_;
    assert(first);
    return first->value_;

  }

  inline const T &
  front() const
  {
    return const_cast<per_node_lock_impl *>(this)->front();
  }

  inline T &
  back()
  {
    unique_lock l(tail_ptr_mutex_); // guards tail from being removed
    assert(head_ != tail_);
    assert(!tail_->next_);
    return tail_->value_;
  }

  inline const T &
  back() const
  {
    return const_cast<per_node_lock_impl *>(this)->back();
  }

  void
  pop_front()
  {
    unique_lock l(head_->mutex_);
    node_ptr first = head_->next_;
    assert(first);
    unique_lock l0(first->mutex_);
    bool is_tail = !first->next_;
    if (is_tail) {
      tail_ptr_mutex_.lock();
      assert(tail_ == first);
    }
    head_->next_ = first->next_;
    if (is_tail) {
      tail_ = head_;
      tail_ptr_mutex_.unlock();
    }
  }

  void
  push_back(const T &val)
  {
    node_ptr n(new node(val, nullptr));
    unique_lock l(tail_ptr_mutex_);
    unique_lock l1(tail_->mutex_);
    assert(!tail_->next_);
    tail_->next_ = n;
    tail_ = n;
  }

  inline void
  remove(const T &val)
  {
    node_ptr prev = head_;
    prev->mutex_.lock();
    node_ptr cur = prev->next_;
    while (cur) {
      cur->mutex_.lock();
      if (cur->value_ == val) {
        // unlink
        bool is_tail = !cur->next_;
        if (is_tail) {
          tail_ptr_mutex_.lock();
          assert(tail_ == cur);
        }
        prev->next_ = cur->next_;
        if (is_tail) {
          tail_ = prev;
          tail_ptr_mutex_.unlock();
        }
        cur->mutex_.unlock();
        cur = prev->next_;
      } else {
        prev->mutex_.unlock();
        prev = cur;
        cur = cur->next_;
      }
    }
    prev->mutex_.unlock();
  }

  std::pair<bool, T>
  try_pop_front()
  {
    unique_lock l(head_->mutex_);
    node_ptr first = head_->next_;
    if (!first)
      return std::make_pair(false, T());
    unique_lock l0(first->mutex_);
    T t = first->value_;
    bool is_tail = !first->next_;
    if (is_tail) {
      tail_ptr_mutex_.lock();
      assert(tail_ == first);
    }
    head_->next_ = first->next_;
    if (is_tail) {
      tail_ = head_;
      tail_ptr_mutex_.unlock();
    }
    return std::make_pair(true, t);
  }

  iterator
  begin()
  {
    unique_lock l(head_->mutex_);
    if (head_->next_)
      return iterator_(std::make_shared<unique_lock>(head_->next_->mutex_), head_->next_);
    else
      return iterator_(unique_lock_ptr(), node_ptr());
  }

  iterator
  end()
  {
    return iterator_(unique_lock_ptr(), node_ptr());
  }
};
