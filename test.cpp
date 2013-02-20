#include <cassert>
#include <iostream>
#include <initializer_list>

#include "linked_list.hpp"
#include "global_lock_impl.hpp"
#include "per_node_lock_impl.hpp"
#include "lock_free_impl.hpp"

#include "rcu.hpp"
#include "atomic_reference.hpp"

using namespace std;

static bool deleted = false;

class foo : public atomic_ref_counted {
public:
  ~foo()
  {
    deleted = true;
  }
};

static void
atomic_ref_ptr_tests()
{
  deleted = false;
  {
    atomic_ref_ptr<foo> p(new foo);
    assert(!p.get_mark());
  }
  assert(deleted);
  deleted = false;

  {
    atomic_ref_ptr<foo> p(new foo);
    assert(!p.get_mark());
    assert(p.mark());
  }
  assert(deleted);
  deleted = false;

  {
    atomic_ref_ptr<foo> p(new foo);
    p = atomic_ref_ptr<foo>();
  }
  assert(deleted);
}

template <typename IterA, typename IterB>
static void
AssertEqualRanges(IterA begin_a, IterA end_a, IterB begin_b, IterB end_b)
{
  while (begin_a != end_a && begin_b != end_b) {
    assert(*begin_a == *begin_b);
    ++begin_a; ++begin_b;
  }
  assert(begin_a == end_a);
  assert(begin_b == end_b);
}

template <typename Iter>
static void
AssertEqual(Iter begin, Iter end, initializer_list<typename Iter::value_type> list)
{
  AssertEqualRanges(begin, end, list.begin(), list.end());
}

template <typename Impl>
static void
single_threaded_tests()
{
  typedef linked_list<int, Impl> llist;

  llist l;
  l.push_back(1);
  assert(l.front() == 1);
  l.push_back(2);
  assert(l.front() == 1);

  AssertEqual(l.begin(), l.end(), {1, 2});

  l.pop_front();
  assert(l.front() == 2);
  l.pop_front();
  assert(l.empty());

  l.push_back(10);
  l.push_back(10);
  l.push_back(20);
  l.push_back(30);
  l.push_back(50);
  l.push_back(10);
  AssertEqual(l.begin(), l.end(), {10, 10, 20, 30, 50, 10});

  l.remove(10);
  for (typename llist::iterator it = l.begin(); it != l.end(); ++it) {
    assert(*it != 10);
  }
  AssertEqual(l.begin(), l.end(), {20, 30, 50});
}

template <typename Function>
static void
ExecTest(Function &&f, const string &name)
{
  f();
  cout << "Test " << name << " passed" << endl;
}

template <typename T>
struct ll_policy {
  typedef global_lock_impl<T> global_lock;
  typedef per_node_lock_impl<T> per_node_lock;
  typedef lock_free_impl<T> lock_free;
  typedef lock_free_impl<T, nop_ref_counted, scoped_rcu_region> lock_free_rcu;
};

int
main(int argc, char **argv)
{
  ExecTest(atomic_ref_ptr_tests, "atomic_ref_ptr");
  ExecTest(single_threaded_tests<typename ll_policy<int>::global_lock>, "global_lock");
  ExecTest(single_threaded_tests<typename ll_policy<int>::per_node_lock>, "per_node_locks");
  ExecTest(single_threaded_tests<typename ll_policy<int>::lock_free>, "lock_free");
  ExecTest(single_threaded_tests<typename ll_policy<int>::lock_free_rcu>, "lock_free_rcu");
  return 0;
}
