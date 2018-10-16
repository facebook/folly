#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <pool.h>

#include <pushmi/o/transform.h>
#include <pushmi/o/via.h>

using namespace pushmi::aliases;

struct f_t {};
f_t f(){ 
  return {};
}
struct g_t {};
g_t g(f_t){ 
  return {};
}

// these expressions are read backward, bottom-right to top-left
template<class CPUExecutor, class IOExecutor>
void lisp(CPUExecutor cpu, IOExecutor io) {
  // f on cpu - g on cpu (implicit: a single task on the cpu executor runs all the functions)
  op::submit([](g_t){})(
    op::transform([](f_t ft) {return g(ft);})(
        op::transform([](auto){ return f(); })(
          cpu)));

  // f on cpu - g on cpu (explicit: the first cpu task runs f and a second cpu task runs g)
  op::submit([](g_t){})(
    op::transform([](f_t ft) {return g(ft);})(
      op::via([cpu]{return cpu;})(
        op::transform([](auto){ return f(); })(
          cpu))));

  // f on io  - g on cpu
  op::submit([](g_t){})(
    op::transform([](f_t ft) {return g(ft);})(
      op::via([cpu]{return cpu;})(
        op::transform([](auto){ return f(); })(
          io))));
}

template<class CPUExecutor, class IOExecutor>
void sugar(CPUExecutor cpu, IOExecutor io) {
  // f on cpu - g on cpu (implicit: a single task on the cpu executor runs all the functions)
  cpu | 
    op::transform([](auto){ return f(); }) | 
    op::transform([](f_t ft) {return g(ft);}) | 
    op::submit([](g_t){});

  // f on cpu - g on cpu (explicit: the first cpu task runs f and a second cpu task runs g)
  cpu | 
    op::transform([](auto){ return f(); }) | 
    op::via([cpu]{return cpu;}) | 
    op::transform([](f_t ft) {return g(ft);}) | 
    op::submit([](g_t){});

  // f on io  - g on cpu
  io | 
    op::transform([](auto){ return f(); }) | 
    op::via([cpu]{return cpu;}) | 
    op::transform([](f_t ft) {return g(ft);}) | 
    op::submit([](g_t){});
}

template<class CPUExecutor, class IOExecutor>
void pipe(CPUExecutor cpu, IOExecutor io) {
  // f on cpu - g on cpu (implicit: a single task on the cpu executor runs all the functions)
  mi::pipe(
    cpu, 
    op::transform([](auto){ return f(); }), 
    op::transform([](f_t ft) {return g(ft);}), 
    op::submit([](g_t){}));

  // f on cpu - g on cpu (explicit: the first cpu task runs f and a second cpu task runs g)
  mi::pipe(
    cpu, 
    op::transform([](auto){ return f(); }), 
    op::via([cpu]{return cpu;}), 
    op::transform([](f_t ft) {return g(ft);}), 
    op::submit([](g_t){}));

  // f on io  - g on cpu
  mi::pipe(
    io, 
    op::transform([](auto){ return f(); }), 
    op::via([cpu]{return cpu;}), 
    op::transform([](f_t ft) {return g(ft);}), 
    op::submit([](g_t){}));
}

int main()
{
  mi::pool cpuPool{std::max(1u,std::thread::hardware_concurrency())};
  mi::pool ioPool{std::max(1u,std::thread::hardware_concurrency())};

  lisp(cpuPool.executor(), ioPool.executor());
  sugar(cpuPool.executor(), ioPool.executor());
  pipe(cpuPool.executor(), ioPool.executor());

  ioPool.wait();
  cpuPool.wait();

  std::cout << "OK" << std::endl;
}
