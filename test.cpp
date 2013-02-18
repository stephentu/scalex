#include <cassert>
#include <iostream>

#include "linked_list.hpp"
#include "global_lock_impl.hpp"

using namespace std;

int
main(int argc, char **argv)
{
  typedef
    linked_list<int, global_lock_impl<int>>
    gl_linked_list;

  gl_linked_list l;
  l.push_back(1);
  assert(l.front() == 1);
  l.push_back(2);
  assert(l.front() == 1);
  for (gl_linked_list::iterator it = l.begin(); it != l.end(); ++it)
    cout << *it << endl;
  l.pop_front();
  assert(l.front() == 2);
  l.pop_front();
  assert(l.empty());
  return 0;
}
