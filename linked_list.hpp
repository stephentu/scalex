#pragma once

#include <cstddef>

/**
 * We define a common linked-list interface, to make writing
 * benchmarks easier
 */
template <typename T, typename Impl>
class linked_list {
public:

  typedef T value_type;
  typedef T & reference;
  typedef const T & const_reference;

  typedef typename Impl::iterator iterator;

  linked_list() : impl_() {}

  inline bool
  empty() const
  {
    return impl_.size() == 0;
  }

  inline size_t
  size() const
  {
    return impl_.size();
  }

  inline reference
  front()
  {
    return impl_.front();
  }

  inline const_reference
  front() const
  {
    return impl_.front();
  }

  inline void
  pop_front()
  {
    impl_.pop_front();
  }

  inline void
  push_back(const value_type &val)
  {
    impl_.push_back(val);
  }

  inline iterator
  begin()
  {
    return impl_.begin();
  }

  inline iterator
  end()
  {
    return impl_.end();
  }

private:
  Impl impl_;
};
