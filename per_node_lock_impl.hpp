#pragma once

#include <cassert>
#include <memory>

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

    mutable lock_type mutex_;
    T value_;
    node_ptr next_;
  };

  node_ptr head_; // head_ points to a sentinel beginning node

  struct iterator_ {
    iterator_() : lock_(), node_() {}
    iterator_(const unique_lock_ptr &lock, const node_ptr &node)
      : lock_(lock), node_(node) {}

    typedef T value_type;

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

  per_node_lock_impl() : head_(new node) {}

  size_t
  size() const
  {
    size_t ret = 0;
    head_->mutex_.lock();
    node_ptr prev = head_;
    node_ptr cur = head_->next_;
    while (cur) {
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
    head_->mutex_.lock();
    node_ptr first = head_->next_;
    assert(first);
    unique_lock l(first->mutex_);
    head_->mutex_.unlock();
    return first->value_;

  }

  inline const T &
  front() const
  {
    head_->mutex_.lock();
    node_ptr first = head_->next_;
    assert(first);
    unique_lock l(first->mutex_);
    head_->mutex_.unlock();
    return first->value_;
  }

  void
  pop_front()
  {
    unique_lock l(head_->mutex_);
    node_ptr first = head_->next_;
    assert(first);
    unique_lock l0(first->mutex_);
    head_->next_ = first->next_;
  }

  void
  push_back(const T &val)
  {
    node_ptr prev = head_;
    prev->mutex_.lock();
    node_ptr cur = prev->next_;
    while (cur) {
      cur->mutex_.lock();
      prev->mutex_.unlock();
      prev = cur;
      cur = cur->next_;
    }
    prev->next_ = node_ptr(new node(val, nullptr));
    prev->mutex_.unlock();
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
        prev->next_ = cur->next_;
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
    head_->next_ = first->next_;
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
