#pragma once

#include <cstddef>
#include <utility>

/**
 * We define a common linked-list interface, to make writing benchmarks easier:
 * this implementation doesn't really do much other than delegate to the
 * underlying implementation
 *
 * We try to use an API similar to a subset of the std::list API, with a
 * few non-standard functions which make more sense in a multi-threaded
 * environment.
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

  inline void
  remove(const value_type &val)
  {
    impl_.remove(val);
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

  // begin non-standard API

  std::pair<bool, T>
  try_pop_front()
  {
    return impl_.try_pop_front();
  }

private:
  Impl impl_;
};
