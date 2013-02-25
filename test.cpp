#include <cassert>
#include <iostream>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <thread>

#include "policy.hpp"
#include "asm.hpp"
#include "rcu.hpp"
#include "macros.hpp"
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
    ASSERT(!p.get_mark());
  }
  ASSERT(deleted);
  deleted = false;

  {
    atomic_ref_ptr<foo> p(new foo);
    ASSERT(!p.get_mark());
    ASSERT(p.mark());
  }
  ASSERT(deleted);
  deleted = false;

  {
    atomic_ref_ptr<foo> p(new foo);
    p = atomic_ref_ptr<foo>();
  }
  ASSERT(deleted);
  deleted = false;

  {
    atomic_ref_ptr<foo> p0(new foo);
    atomic_ref_ptr<foo> p1(new foo);
    p0 = p1;
    ASSERT(deleted);
    deleted = false;
  }
  ASSERT(deleted);
  deleted = false;
}

template <typename IterA, typename IterB>
static void
AssertEqualRanges(IterA begin_a, IterA end_a, IterB begin_b, IterB end_b)
{
  while (begin_a != end_a && begin_b != end_b) {
    ASSERT(*begin_a == *begin_b);
    ++begin_a; ++begin_b;
  }
  ASSERT(begin_a == end_a);
  ASSERT(begin_b == end_b);
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
  ASSERT(l.empty());

  l.push_back(1);
  ASSERT(l.front() == 1);
  ASSERT(l.back() == 1);
  ASSERT(l.size() == 1);
  AssertEqual(l.begin(), l.end(), {1});

  l.push_back(2);
  ASSERT(l.front() == 1);
  ASSERT(l.back() == 2);
  ASSERT(l.size() == 2);
  AssertEqual(l.begin(), l.end(), {1, 2});

  l.pop_front();
  ASSERT(l.front() == 2);
  ASSERT(l.back() == 2);
  ASSERT(l.size() == 1);
  AssertEqual(l.begin(), l.end(), {2});

  l.pop_front();
  ASSERT(l.empty());

  l.push_back(10);
  l.push_back(10);
  l.push_back(20);
  l.push_back(30);
  l.push_back(50);
  l.push_back(10);
  ASSERT(l.front() == 10);
  ASSERT(l.back() == 10);
  ASSERT(l.size() == 6);
  AssertEqual(l.begin(), l.end(), {10, 10, 20, 30, 50, 10});

  l.remove(10);
  for (typename llist::iterator it = l.begin(); it != l.end(); ++it) {
    ASSERT(*it != 10);
  }
  ASSERT(l.front() == 20);
  ASSERT(l.back() == 50);
  ASSERT(l.size() == 3);
  AssertEqual(l.begin(), l.end(), {20, 30, 50});

  auto ret = l.try_pop_front();
  ASSERT(ret.first);
  ASSERT(ret.second == 20);
  ASSERT(l.front() == 30);
  ASSERT(l.back() == 50);
  ASSERT(l.size() == 2);
}

// there's probably a better way to do this
static vector<int>
range(int range_begin, int range_end)
{
  ASSERT(range_end >= range_begin); // cant handle this for now
  vector<int> r;
  r.reserve(range_end - range_begin);
  for (int i = range_begin; i < range_end; i++)
    r.push_back(i);
  return r;
}

template <typename Impl>
static void
llist_insert(linked_list<int, Impl> &l, atomic<bool> &f, int range_begin, int range_end)
{
  while (!f.load())
    nop_pause();
  for (int i = range_begin; i < range_end; i++)
    l.push_back(i);
}

template <typename Impl>
static void
llist_pop_front(linked_list<int, Impl> &l, atomic<bool> &f, atomic<bool> &can_stop, vector<int> &popped)
{
  while (!f.load())
    nop_pause();
  for (;;) {
    auto ret = l.try_pop_front();
    if (!ret.first && can_stop.load())
      break;
    if (ret.first)
      popped.push_back(ret.second);
  }
}

template <typename Impl>
static void
llist_remove(linked_list<int, Impl> &l, atomic<bool> &f, int range_begin, int range_end)
{
  while (!f.load())
    nop_pause();
  for (int i = range_begin; i < range_end; i++)
    l.remove(i);
}

template <typename Impl>
static void
llist_push_back(linked_list<int, Impl> &l, atomic<bool> &f, int range_begin, int range_end)
{
  while (!f.load())
    nop_pause();
  for (int i = range_begin; i < range_end; i++)
    l.push_back(i);
}

template <typename Impl>
static void
multi_threaded_tests()
{
  typedef linked_list<int, Impl> llist;

  // try a bunch of concurrent inserts, make sure we don't lose
  // any values
  {
    llist l;
    const int NElemsPerThread = 2000;
    const int NThreads = 4;
    vector<thread> thds;
    atomic<bool> start_flag(false);
    for (int i = 0; i < NThreads; i++) {
      thread t(llist_insert<Impl>, ref(l), ref(start_flag), i * NElemsPerThread, (i + 1) * NElemsPerThread);
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    vector<int> ll_elems(l.begin(), l.end());
    sort(ll_elems.begin(), ll_elems.end());
    ASSERT(ll_elems == range(0, NThreads * NElemsPerThread));
  }

  // try a bunch of concurrent try_pop_fronts, make sure we see every element
  {
    llist l;
    const int NElems = 2000;
    const int NThreads = 4;
    for (auto e : range(0, NElems))
      l.push_back(e);
    vector<thread> thds;
    vector<vector<int>> results;
    results.resize(NThreads);
    atomic<bool> start_flag(false);
    atomic<bool> can_stop(true);
    for (int i = 0; i < NThreads; i++) {
      thread t(llist_pop_front<Impl>, ref(l), ref(start_flag), ref(can_stop), ref(results[i]));
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    ASSERT(l.empty());
    vector<int> ll_elems;
    for (auto &r : results)
      ll_elems.insert(ll_elems.end(), r.begin(), r.end());
    sort(ll_elems.begin(), ll_elems.end());
    ASSERT(ll_elems == range(0, NElems));
  }

  // try a bunch of concurrent removes (w/ no inserts). make sure we remove all
  // the elements
  {
    llist l;
    const int NElemsPerThread = 2000;
    const int NThreads = 4;
    for (auto e : range(0, NThreads * NElemsPerThread))
      l.push_back(e);
    ASSERT(l.size() == (NThreads * NElemsPerThread));
    vector<thread> thds;
    atomic<bool> start_flag(false);
    for (int i = 0; i < NThreads; i++) {
      thread t(llist_remove<Impl>, ref(l), ref(start_flag), i * NElemsPerThread, (i + 1) * NElemsPerThread);
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    ASSERT(l.empty());
  }

  // try non conflicting remove/push_backs, make sure we don't lose any of the
  // push_backs
  {
    llist l;
    const int NElemsPerThread = 2000;
    const int NRemoveThreads = 4;
    const int NPushBackThreads = 4;
    const int Base = NRemoveThreads * NElemsPerThread;
    for (auto e : range(0, Base))
      l.push_back(e);
    ASSERT(l.size() == (size_t) Base);
    vector<thread> thds;
    atomic<bool> start_flag(false);
    for (int i = 0; i < NRemoveThreads; i++) {
      thread t(llist_remove<Impl>, ref(l), ref(start_flag), i * NElemsPerThread, (i + 1) * NElemsPerThread);
      thds.push_back(move(t));
    }
    for (int i = 0; i < NPushBackThreads; i++) {
      thread t(llist_push_back<Impl>, ref(l), ref(start_flag),
          Base + i * NElemsPerThread, Base + (i + 1) * NElemsPerThread);
      thds.push_back(move(t));
    }
    start_flag.store(true);
    for (auto &t : thds)
      t.join();
    vector<int> ll_elems(l.begin(), l.end());
    sort(ll_elems.begin(), ll_elems.end());
    ASSERT(ll_elems == range(Base, Base + (NPushBackThreads * NElemsPerThread)));
  }

  // try as a producer/consumer queue
  {
    llist l;
    atomic<bool> start_flag(false);
    thread pusher(llist_push_back<Impl>, ref(l), ref(start_flag), 0, 10000);
    vector<int> popped;
    atomic<bool> can_stop(false);
    thread popper(llist_pop_front<Impl>, ref(l), ref(start_flag), ref(can_stop), ref(popped));
    start_flag.store(true);
    pusher.join();
    can_stop.store(true);
    popper.join();
    ASSERT(popped == range(0, 10000));
  }
}

template <typename Function>
static void
ExecTest(Function &&f, const string &name)
{
  f();
  cout << "Test " << name << " passed" << endl;
}

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
