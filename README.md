scalex
=============

This project demonstrates various techniques for programming concurrent data
structures on modern multicore machines. A linked-list is used as the data
structure of study. Even though it is the simplest of sequential data
structures, a scalable concurrent implementation still requires some thought.

Build
------
These examples are written in C++11. In order to compile the code, you need a
relatively recent version of GCC which supports C++11. There are no external
library dependencies. Simply run:

    make # for tests
    make bench # for benchmark program

Running
-------
For test suite
    ./test

For benchmark
    ./bench [--verbose] \
      --bench (readonly|queue) \
      --policy (global_lock|per_node_lock|lock_free|lock_free_rcu) \
      --num-threads nthreads \
      --runtime nsec
