#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <set>
#include <memory>

#include <unistd.h> // for sleep()
#include <getopt.h>

#include "policy.hpp"
#include "asm.hpp"
#include "rcu.hpp"
#include "timer.hpp"

using namespace std;

// some global variables - set defaults here
static int g_verbose = false;
static size_t g_nthreads = 1;
static uint64_t g_duration_sec = 10;

static void
_die(const char *filename,
     const char *func,
     int line,
     const string &msg) __attribute__((noreturn));
static void
_die(const char *filename,
     const char *func,
     int line,
     const string &msg)
{
  cerr << filename << ":" << line << ": " << func << " - panic: " << msg << endl;
  abort();
}

#define die(x) _die(__FILE__, __func__, __LINE__, x)

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
    size_t agg_ops = 0;
    for (auto &w : workers) {
      if (g_verbose)
        cout << w->name << " : " << double(w->nops)/elasped_sec << " ops/sec" << endl;
      agg_ops += w->nops;
    }
    if (g_verbose)
      cout << "total : " << double(agg_ops)/elasped_sec << " ops/sec" << endl;
    else
      // output for runner.py
      cout << double(agg_ops)/elasped_sec << endl;
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
    ro_worker(llist *list) : worker("reader"), list(list), nelems_seen(0) {}
    inline size_t get_nelems_seen() const { return nelems_seen; }
  protected:
    void
    run(const atomic<bool> &stop_flag) OVERRIDE
    {
      while (!stop_flag.load()) {
        vector<int> l(list->begin(), list->end());
        nelems_seen += l.size(); // so GCC doesn't optimize the vector away
        nops++;
      }
    }
  private:
    llist *list;
    size_t nelems_seen;
  };

protected:
  void
  init() OVERRIDE
  {
    for (size_t i = 0; i < NElems; i++)
      list.push_back(i);
  }

  vector<unique_ptr<worker>>
  make_workers() OVERRIDE
  {
    vector<unique_ptr<worker>> ret;
    for (size_t i = 0; i < g_nthreads; i++)
      ret.emplace_back(new ro_worker(&list));
    return move(ret);
  }

private:
  llist list;
};

template <typename Impl>
class queue_benchmark : public benchmark {
  typedef linked_list<int, Impl> llist;
  static const size_t NElemsInitial = 10000;

  class producer : public worker {
  public:
    producer(llist *list) : worker("producer"), list(list) {}
  protected:
    void
    run(const atomic<bool> &stop_flag) OVERRIDE
    {
      while (!stop_flag.load()) {
        list->push_back(1);
        nops++;
      }
    }
  private:
    llist *list;
  };

  class consumer : public worker {
  public:
    consumer(llist *list) : worker("consumer"), list(list), nelems_popped(0) {}
    inline size_t get_nelems_popped() const { return nelems_popped; }
  protected:
    void
    run(const atomic<bool> &stop_flag) OVERRIDE
    {
      while (!stop_flag.load()) {
        auto ret = list->try_pop_front();
        if (ret.first)
          nelems_popped++;
        nops++; // count regardless of removal or not
      }
    }
  private:
    llist *list;
    size_t nelems_popped;
  };

protected:
  void
  init() OVERRIDE
  {
    for (size_t i = 0; i < NElemsInitial; i++)
      list.push_back(i);
  }

  vector<unique_ptr<worker>>
  make_workers() OVERRIDE
  {
    vector<unique_ptr<worker>> ret;
    for (size_t i = 0; i < g_nthreads / 2; i++)
      ret.emplace_back(new producer(&list));
    for (size_t i = g_nthreads / 2; i < g_nthreads; i++)
      ret.emplace_back(new consumer(&list));
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

  string bench_type = "readonly";
  string policy_type = "global_lock";
  for (;;) {
    static struct option long_options[] =
    {
      {"verbose",      no_argument,       &g_verbose, 1 },
      {"bench",        required_argument, 0,         'b'},
      {"policy",       required_argument, 0,         'p'},
      {"num-threads",  required_argument, 0,         't'},
      {"runtime",      required_argument, 0,         'r'},
      {0, 0, 0, 0}
    };
    int option_index = 0;
    int c = getopt_long(argc, argv, "vb:t:r:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0:
      if (long_options[option_index].flag != 0)
        break;
      abort();
      break;

    case 'b':
      bench_type = optarg;
      break;

    case 'p':
      policy_type = optarg;
      break;

    case 't':
      g_nthreads = strtoul(optarg, NULL, 10);
      if (g_nthreads <= 0)
        die("need --num-threads > 0");
      break;

    case 'r':
      g_duration_sec = strtoul(optarg, NULL, 10);
      if (g_duration_sec <= 0)
        die("need --runtime > 0");
      break;

    case '?':
      /* getopt_long already printed an error message. */
      break;

    default:
      abort();
    }
  }

  const set<string> valid_bench_types =
    {"readonly", "queue"};
  const set<string> valid_policy_types =
    {"global_lock", "per_node_lock", "lock_free", "lock_free_rcu"};

  if (!valid_bench_types.count(bench_type))
    die("invalid --bench");

  if (!valid_policy_types.count(policy_type))
    die("invalid --policy");

  unique_ptr<benchmark> p;

  // XXX(stephentu): there must be a better way to do this, but leave it for
  // now
  if (bench_type == "readonly") {
    if (policy_type == "global_lock")
      p.reset(new read_only_benchmark<typename ll_policy<int>::global_lock>);
    else if (policy_type == "per_node_lock")
      p.reset(new read_only_benchmark<typename ll_policy<int>::per_node_lock>);
    else if (policy_type == "lock_free")
      p.reset(new read_only_benchmark<typename ll_policy<int>::lock_free>);
    else if (policy_type == "lock_free_rcu")
      p.reset(new read_only_benchmark<typename ll_policy<int>::lock_free_rcu>);
  } else if (bench_type == "queue") {
    if (policy_type == "global_lock")
      p.reset(new queue_benchmark<typename ll_policy<int>::global_lock>);
    else if (policy_type == "per_node_lock")
      p.reset(new queue_benchmark<typename ll_policy<int>::per_node_lock>);
    else if (policy_type == "lock_free")
      p.reset(new queue_benchmark<typename ll_policy<int>::lock_free>);
    else if (policy_type == "lock_free_rcu")
      p.reset(new queue_benchmark<typename ll_policy<int>::lock_free_rcu>);
  }

  if (g_verbose) {
    cout << "bench configuration:" << endl
         << "  bench      : " << bench_type << endl
         << "  policy     : " << policy_type << endl
         << "  num-threads: " << g_nthreads << endl
         << "  runtime    : " << g_duration_sec << " sec" << endl;
  }

  p->do_bench();
  return 0;
}
