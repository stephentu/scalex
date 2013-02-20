#include <cassert>
#include <iostream>

#include "linked_list.hpp"
#include "global_lock_impl.hpp"
#include "per_node_lock_impl.hpp"
#include "lock_free_impl.hpp"

#include "atomic_reference.hpp"

using namespace std;

static bool deleted = false;

class foo : public atomic_ref_counted {
public:
  ~foo()
  {
    cout << "deleted" << endl;
    deleted = true;
  }
};

int
main(int argc, char **argv)
{
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

  typedef
    //linked_list<int, global_lock_impl<int>>
    //linked_list<int, per_node_lock_impl<int>>
    linked_list<int, lock_free_impl<int>>
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

  l.push_back(10);
  l.push_back(10);
  l.push_back(20);
  l.push_back(30);
  l.push_back(50);
  l.push_back(10);
  for (gl_linked_list::iterator it = l.begin(); it != l.end(); ++it)
    cout << *it << endl;

  l.remove(10);
  cout << "---" << endl;
  for (gl_linked_list::iterator it = l.begin(); it != l.end(); ++it) {
    cout << *it << endl;
    assert(*it != 10);
  }

  return 0;
}
