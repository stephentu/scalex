#pragma once

#include <cassert>
#include <memory>
#include <mutex>

/**
 * Standard singly-linked list with a global lock for protection, and
 * standard reference counting
 *
 * References returned by this implementation are guaranteed to be valid
 * until the element is removed from the list
 */
template <typename T>
class global_lock_impl {
private:

  struct node;
  typedef std::unique_lock<std::mutex> unique_lock;
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

    T value_;
    node_ptr next_;
  };

  mutable std::mutex mutex_;
  node_ptr head_;

  struct iterator_ {
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
      node_ = node_->next_;
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

  global_lock_impl() : mutex_(), head_() {}

  size_t
  size() const
  {
    unique_lock l(mutex_);
    size_t ret = 0;
    node_ptr cur = head_;
    while (cur) {
      ret++;
      cur = cur->next_;
    }
    return ret;
  }

  inline T &
  front()
  {
    unique_lock l(mutex_);
    assert(head_);
    return head_->value_;
  }

  inline const T &
  front() const
  {
    unique_lock l(mutex_);
    assert(head_);
    return head_->value_;
  }

  void
  pop_front()
  {
    unique_lock l(mutex_);
    assert(head_);
    node_ptr next = head_->next_;
    head_ = next;
  }

  void
  push_back(const T &val)
  {
    unique_lock l(mutex_);
    node_ptr p = head_, *pp = &head_;
    for (; p; pp = &p->next_, p = p->next_)
      ;
    node_ptr n(new node(val, nullptr));
    *pp = n;
  }

  inline void
  remove(const T &val)
  {
    unique_lock l(mutex_);
    node_ptr p = head_, *pp = &head_;
    while (p) {
      if (p->value_ == val) {
        // unlink
        *pp = p->next_;
        p = *pp;
      } else {
        pp = &p->next_;
        p = p->next_;
      }
    }
  }

  iterator
  begin()
  {
    unique_lock_ptr l(new unique_lock(mutex_));
    return iterator_(l, head_);
  }

  iterator
  end()
  {
    return iterator_(unique_lock_ptr(), node_ptr());
  }
};
