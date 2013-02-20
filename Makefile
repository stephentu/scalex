CXX := g++-4.7
CXXFLAGS := -Wall -Werror -g --std=c++0x
LDFLAGS := -lpthread -lrt

HEADERS = macros.hpp \
	  spinlock.hpp \
	  rcu.hpp \
	  util.hpp \
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
