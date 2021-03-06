-include config.mk
CXXFLAGS := -Wall -Werror -g -O2 --std=c++0x -DNDEBUG
LDFLAGS := -lpthread -lrt

# 0 = libc malloc
# 1 = jemalloc
# 2 = tcmalloc
USE_MALLOC_MODE=1

ifeq ($(USE_MALLOC_MODE),1)
        CXXFLAGS+=-DUSE_JEMALLOC
        LDFLAGS+=-ljemalloc
else
ifeq ($(USE_MALLOC_MODE),2)
        CXXFLAGS+=-DUSE_TCMALLOC
        LDFLAGS+=-ltcmalloc
endif
endif

HEADERS = macros.hpp \
	  spinlock.hpp \
	  rcu.hpp \
	  util.hpp \
	  timer.hpp \
	  policy.hpp \
	  linked_list.hpp \
	  global_lock_impl.hpp \
	  per_node_lock_impl.hpp \
	  lock_free_impl.hpp \
	  atomic_reference.hpp

SRCFILES = rcu.cpp
OBJFILES = $(SRCFILES:.cpp=.o)

all: test

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test.o $(OBJFILES)
	$(CXX) -o test $^ $(LDFLAGS)

bench: bench.o $(OBJFILES)
	$(CXX) -o bench $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f test bench *.o
