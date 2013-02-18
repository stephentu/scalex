CXXFLAGS := -Wall -Werror -g --std=c++0x
LDFLAGS := -lpthread -lrt

HEADERS = linked_list.hpp \
	  global_lock_impl.hpp
SRCFILES = 
OBJFILES = $(SRCFILES:.cpp=.o)

all: test

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test.o $(OBJFILES)
	$(CXX) -o test $^ $(LDFLAGS) 
