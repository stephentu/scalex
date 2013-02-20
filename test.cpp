#include <cassert>
#include <iostream>
#include <initializer_list>
#include <vector>
#include <algorithm>

#include "linked_list.hpp"
#include "global_lock_impl.hpp"
#include "per_node_lock_impl.hpp"
#include "lock_free_impl.hpp"

#include "asm.hpp"
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
  deleted = false;

  {
    atomic_ref_ptr<foo> p0(new foo);
    atomic_ref_ptr<foo> p1(new foo);
    p0 = p1;
    assert(deleted);
    deleted = false;
  }
  assert(deleted);
  deleted = false;
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

  auto ret = l.try_pop_front();
  assert(ret.first);
  assert(ret.second == 20);
}

// there's probably a better way to do this
static vector<int>
range(int range_begin, int range_end)
{
  assert(range_end >= range_begin); // cant handle this for now
  vector<int> r;
  r.reserve(range_end - range_begin);
  for (int i = range_begin; i < range_end; i++)
    r.push_back(i);
  return r;
}

template <typename Impl>
static void
ilist_insert(linked_list<int, Impl> &l, atomic<bool> &f, int range_begin, int range_end)
{
  while (!f.load())
    nop_pause();
  for (int i = range_begin; i < range_end; i++)
    l.push_back(i);
}

template <typename Impl>
static void
ilist_pop_front(linked_list<int, Impl> &l, atomic<bool> &f, vector<int> &popped)
{
  while (!f.load())
    nop_pause();
  for (;;) {
    auto ret = l.try_pop_front();
    if (!ret.first)
      break;
    popped.push_back(ret.second);
  }
}

template <typename Impl>
static void
multi_threaded_tests()
{
  typedef linked_list<int, Impl> llist;

  // try a bunch of concurrent inserts, make sure we don't lose
  // any values!
  {
    llist l;
    const int NElemsPerThread = 500;
    const int NThreads = 4;
    vector<thread> thds;
    atomic<bool> start_flag(false);
    for (int i = 0; i < NThreads; i++) {
      thread t(ilist_insert<Impl>, ref(l), ref(start_flag), i * NElemsPerThread, (i + 1) * NElemsPerThread);
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    vector<int> ll_elems(l.begin(), l.end());
    sort(ll_elems.begin(), ll_elems.end());
    assert(ll_elems == range(0, NThreads * NElemsPerThread));
  }

  // try a bunch of concurrent try_pop_fronts, make sure we see every element
  {
    llist l;
    const int NElems = 500;
    const int NThreads = 4;
    for (auto e : range(0, NElems))
      l.push_back(e);
    vector<thread> thds;
    vector<vector<int>> results;
    results.resize(NThreads);
    atomic<bool> start_flag(false);
    for (int i = 0; i < NThreads; i++) {
      thread t(ilist_pop_front<Impl>, ref(l), ref(start_flag), ref(results[i]));
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    assert(l.empty());
    vector<int> ll_elems;
    for (auto &r : results)
      ll_elems.insert(ll_elems.end(), r.begin(), r.end());
    sort(ll_elems.begin(), ll_elems.end());
    assert(ll_elems == range(0, NElems));
  }
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

  ExecTest(single_threaded_tests<typename ll_policy<int>::global_lock>, "single-threaded global_lock");
  ExecTest(single_threaded_tests<typename ll_policy<int>::per_node_lock>, "single-threaded per_node_locks");
  ExecTest(single_threaded_tests<typename ll_policy<int>::lock_free>, "single-threaded lock_free");
  ExecTest(single_threaded_tests<typename ll_policy<int>::lock_free_rcu>, "single-threaded lock_free_rcu");

  ExecTest(multi_threaded_tests<typename ll_policy<int>::global_lock>, "multi-threaded global_lock");
  ExecTest(multi_threaded_tests<typename ll_policy<int>::per_node_lock>, "multi-threaded per_node_locks");
  ExecTest(multi_threaded_tests<typename ll_policy<int>::lock_free>, "multi-threaded lock_free");
  ExecTest(multi_threaded_tests<typename ll_policy<int>::lock_free_rcu>, "multi-threaded lock_free_rcu");
  return 0;
}
