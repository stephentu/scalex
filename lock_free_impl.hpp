#pragma once

#include <cassert>
#include <memory>
#include <iterator>

#include "atomic_reference.hpp"
#include "macros.hpp"

namespace private_ {
struct nop_scoper {
  template <typename T>
  inline void release(T *) const {}
};
}

/**
 * Lock-free singly-linked list implemention, with configurable ref counting
 * and garbage collection policies
 *
 * References returned by this implementation are guaranteed to be valid until
 * the element is removed from the list
 */
template <typename T,
          typename RefPtrLockImpl = spinlock,
          typename RefCountImpl = atomic_ref_counted,
          typename ScopedImpl = private_::nop_scoper>
class lock_free_impl {
private:

  struct node;
  typedef atomic_ref_ptr<node, RefPtrLockImpl> node_ptr;

  struct node : public RefCountImpl {
    // non-copyable
    node(const node &) = delete;
    node(node &&) = delete;
    node &operator=(const node &) = delete;

    node() : value_(), next_() {}
    node(const T &value, const node_ptr &next)
      : value_(value), next_(next) {}

    ~node()
    {
      // sanity check
      assert(next_.get_mark());
    }

    T value_;
    node_ptr next_;

    inline bool
    is_marked() const
    {
      return next_.get_mark();
    }
  };

  node_ptr head_; // head_ points to a sentinel beginning node
  mutable node_ptr tail_; // tail_ is maintained loosely

  struct iterator_ : public std::iterator<std::forward_iterator_tag, T> {
    iterator_() : node_(), scoper_() {}
    iterator_(const node_ptr &node)
      : node_(node), scoper_() {}

    typedef T value_type;

    T &
    operator*() const
    {
      // could return deleted value
      return node_->value_;
    }

    T *
    operator->() const
    {
      // could return deleted value
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
      do {
        node_ = node_->next_;
      } while (node_ && node_->is_marked());
      return *this;
    }

    iterator_
    operator++(int)
    {
      iterator_ cur = *this;
      ++(*this);
      return cur;
    }

    node_ptr node_;
    ScopedImpl scoper_;
  };

public:

  typedef iterator_ iterator;

  lock_free_impl() : head_(new node), tail_(head_) {}
  ~lock_free_impl()
  {
    ScopedImpl scoper;
    // can do this non-thread safe, since we know there are
    // no other mutators
    node_ptr cur = head_;
    while (cur) {
      if (cur->next_.mark())
        scoper.release(cur.get());
      cur = cur->next_;
    }
  }

  size_t
  size() const
  {
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    size_t ret = 0;
    node_ptr cur = head_->next_;
    while (cur) {
      if (!cur->is_marked()) {
        ret++;
      } else {
        // XXX: reap cur for garbage collection
      }
      if (!cur->next_ && !cur->is_marked() && tail_ != cur)
        tail_ = cur;
      cur = cur->next_;
    }
    return ret;
  }

  T &
  front()
  {
  retry:
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    node_ptr p = head_->next_;
    assert(p);
    if (p->is_marked())
      // XXX: reap p for garbage collection
      goto retry;
    T &ref = p->value_;
    if (p->is_marked())
      // XXX: reap p for garbage collection
      goto retry;
    // we have stability on a reference
    if (!p->next_ && tail_ != p)
      tail_ = p;
    return ref;
  }

  inline const T &
  front() const
  {
    return const_cast<lock_free_impl *>(this)->front();
  }

  T &
  back()
  {
  retry:
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    node_ptr tail = tail_;
    assert(tail);
    while (tail->next_) {
      tail = tail->next_;
      if (!tail)
        goto retry;
    }
    if (tail->is_marked()) { // hopefully rare
      // XXX: reap p for garbage collection
      fix_tail_pointer_from_head();
      goto retry;
    }
    tail_ = tail;
    T &ref = tail->value_;
    if (tail->is_marked()) { // see above
      // XXX: reap p for garbage collection
      fix_tail_pointer_from_head();
      goto retry;
    }
    // we have stability on a reference
    return ref;
  }

  inline const T &
  back() const
  {
    return const_cast<lock_free_impl *>(this)->back();
  }

  void
  pop_front()
  {
  retry:
    ScopedImpl scoper;
    assert(!head_->is_marked());
    node_ptr cur = head_->next_;
    assert(cur);

    if (!cur->next_.mark())
      // was concurrently deleted
      goto retry;

    // we don't need to CAS the prev ptr here, because we know that the
    // sentinel node will never be deleted (that is, the first node of a list
    // will *always* be the first node)
    head_->next_ = cur->next_; // semantics of assign() do not copy marked bits
    if (!cur->next_ && tail_ != head_)
      tail_ = head_;
    assert(cur->is_marked());
    scoper.release(cur.get());
  }

  void
  push_back(const T &val)
  {
  retry:
    ScopedImpl scoper;
    assert(!head_->is_marked());
    node_ptr tail = tail_;
    assert(tail);
    while (tail->next_) {
      tail = tail->next_;
      if (!tail)
        goto retry;
    }
    if (tail->is_marked()) { // hopefully rare
      fix_tail_pointer_from_head();
      goto retry;
    }
    node_ptr n(new node(val, node_ptr()));
    if (!tail->next_.compare_exchange_strong(node_ptr(), n)) {
      bool ret = n->next_.mark(); // be pedantic
      if (!ret) assert(false);
      scoper.release(n.get());
      goto retry;
    }
    tail_ = n;
  }

  inline void
  remove(const T &val)
  {
    ScopedImpl scoper;
    node_ptr prev = head_;
    node_ptr p = head_->next_, *pp = &head_->next_;
    while (p) {
      if (p->value_ == val) {
        // mark removed
        if (p->next_.mark()) {
          // try to unlink- ignore success value
          if (pp->compare_exchange_strong(p, p->next_)) {
            // successful unlink, report
            assert(p->is_marked());
            scoper.release(p.get());
          }
          if (!p->next_)
            tail_ = prev;
        }
        // in any case, advance the current ptr, but keep the
        // prev ptr the same
        p = p->next_;
      } else {
        prev = p;
        pp = &p->next_;
        p = p->next_;
      }
    }
  }

  std::pair<bool, T>
  try_pop_front()
  {
  retry:
    ScopedImpl scoper;
    assert(!head_->is_marked());
    node_ptr cur = head_->next_;

    if (unlikely(!cur))
      return std::make_pair(false, T());

    if (!cur->next_.mark())
      // was concurrently deleted
      goto retry;

    T t = cur->value_;
    head_->next_ = cur->next_; // semantics of assign() do not copy marked bits
    if (!cur->next_ && tail_ != head_)
      tail_ = head_;
    assert(cur->is_marked());
    scoper.release(cur.get());
    return std::make_pair(true, t);
  }

  iterator
  begin()
  {
    ScopedImpl scoper UNUSED;
    return iterator_(head_->next_);
  }

  iterator
  end()
  {
    return iterator_(node_ptr());
  }

private:
  // relatively expensive, should be avoided
  void
  fix_tail_pointer_from_head() const
  {
    node_ptr cur = head_->next_;
    node_ptr prev = head_;
    while (cur) {
      prev = cur;
      cur = cur->next_;
    }
    assert(prev);
    tail_ = prev;
  }
};

