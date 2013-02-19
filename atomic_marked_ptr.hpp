#pragma once

template <typename T, typename Impl>
class atomic_marked_ptr {

  /**
   * true if succeeded, false otherwise
   */
  inline bool
  try_mark()
  {
    return false;
  }

  inline bool
  is_marked() const
  {
    return false;
  }

  inline T *
  load() const
  {
    return nullptr;
  }

  T &
  operator*() const
  {
    return *load();
  }

  T *
  operator->() const
  {
    return load();
  }

private:
  Impl impl_;
};
