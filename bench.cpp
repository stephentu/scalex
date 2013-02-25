#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <memory>

#include <unistd.h> // for sleep()

#include "policy.hpp"
#include "asm.hpp"
#include "rcu.hpp"
#include "timer.hpp"

using namespace std;

// some global variables
static size_t g_nthreads = 0;
static uint64_t g_duration_sec = 0;

//static void
//_die(const char *filename,
//     const char *func,
//     int line,
//     const string &msg) __attribute__((noreturn))
//{
//  cerr << filename << ":" << line << ": " << func << " - panic: " << msg << endl;
//  abort();
//}
//
//#define die(x) _die(__FILE__, __func__, __LINE__, x)

class worker {
  friend class benchmark;
public:
  worker(const string &name) : name(name), nops(0) {}
  virtual ~worker() {}

protected:

  static void
  thread_fn(unique_ptr<worker> &w,
            const atomic<bool> &start_flag,
            const atomic<bool> &stop_flag)
  {
    while (!start_flag.load())
      nop_pause();
    w->run(stop_flag);
  }

  // returns the number of ops
  virtual void run(const atomic<bool> &stop_flag) = 0;

  string name;
  atomic<size_t> nops;
};

class benchmark {
public:
  virtual ~benchmark() {}

  void
  do_bench()
  {
    init();
    auto workers = make_workers();
    atomic<bool> start_flag(false);
    atomic<bool> stop_flag(false);
    vector<thread> thds;
    for (auto &w : workers)
      thds.emplace_back(worker::thread_fn, ref(w), ref(start_flag), ref(stop_flag));
    start_flag.store(true);
    timer t;
    sleep(g_duration_sec);
    stop_flag.store(true);
    for (auto &t : thds)
      t.join();
    const uint64_t elasped_usec = t.lap();
    const double elasped_sec = double(elasped_usec) / 1000000.0;
    for (auto &w : workers)
      cout << w->name << " : " << double(w->nops)/elasped_sec << " ops/sec" << endl;
  }

protected:
  virtual void init() = 0;
  virtual vector<unique_ptr<worker>> make_workers() = 0;
};

template <typename Impl>
class read_only_benchmark : public benchmark {
  typedef linked_list<int, Impl> llist;
  static const size_t NElems = 100;

  class ro_worker : public worker {
  public:
    ro_worker(llist *list) : worker("reader"), list(list) {}
  protected:
    void
    run(const atomic<bool> &stop_flag) override
    {
      while (!stop_flag.load()) {
        vector<int> l(list->begin(), list->end());
        nops++;
      }
    }
  private:
    llist *list;
  };

public:

protected:
  void
  init() override
  {
    for (size_t i = 0; i < NElems; i++)
      list.push_back(i);
  }

  vector<unique_ptr<worker>>
  make_workers() override
  {
    vector<unique_ptr<worker>> ret;
    for (size_t i = 0; i < g_nthreads; i++)
      ret.emplace_back(new ro_worker(&list));
    return move(ret);
  }

private:
  llist list;
};

int
main(int argc, char **argv)
{
#ifndef NDEBUG
  cerr << "Warning: benchmarks being run w/ assertions" << endl;
#endif

  g_nthreads = 2;
  g_duration_sec = 5;

  unique_ptr<benchmark> p(new read_only_benchmark<typename ll_policy<int>::global_lock>);
  p->do_bench();

  return 0;
}
