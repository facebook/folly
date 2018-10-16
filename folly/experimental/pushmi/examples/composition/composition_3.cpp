#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <pushmi/o/defer.h>
#include <pushmi/o/share.h>

#include <pushmi/o/just.h>
#include <pushmi/o/tap.h>

using namespace pushmi::aliases;

// three models of submission deferral
//  (none of these use an executor, they are all running
//  synchronously on the main thread)

// this constructs eagerly and submits just() lazily
auto defer_execution() {
  printf("construct just\n");
  return op::just(42) |
    op::tap([](int v){ printf("just - %d\n", v); });
}

// this constructs defer() eagerly, constructs just() and submits just() lazily
auto defer_construction() {
  return op::defer([]{
      return defer_execution();
  });
}

// this constructs defer(), constructs just() and submits just() eagerly
auto eager_execution() {
    return defer_execution() | op::share<int>();
}

int main()
{

  printf("\ncall defer_execution\n");
  auto de = defer_execution();
  printf("submit defer_execution\n");
  de | op::submit();
// call defer_execution
// construct just
// submit defer_execution
// just - 42

  printf("\ncall defer_construction\n");
  auto dc = defer_construction();
  printf("submit defer_construction\n");
  dc | op::submit();
// call defer_construction
// submit defer_construction
// construct just
// just - 42

  printf("\ncall eager_execution\n");
  auto ee = eager_execution();
  printf("submit eager_execution\n");
  ee | op::submit();
// call eager_execution
// construct just
// just - 42
// submit eager_execution

  std::cout << "OK" << std::endl;
// OK
}
