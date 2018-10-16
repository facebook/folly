
#include "pushmi/o/just.h"
#include "pushmi/o/on.h"
#include "pushmi/o/transform.h"
#include "pushmi/o/tap.h"
#include "pushmi/o/via.h"
#include "pushmi/o/submit.h"

#include "pushmi/trampoline.h"
#include "pushmi/new_thread.h"

#include "pool.h"

using namespace pushmi::aliases;

struct countdownsingle {
  countdownsingle(int& c)
      : counter(&c) {}

  int* counter;

  template <class ExecutorRef>
  void operator()(ExecutorRef exec) {
    if (--*counter > 0) {
      exec | op::submit(*this);
    }
  }
};

#define concept Concept
#include <nonius/nonius.h++>

NONIUS_BENCHMARK("trampoline virtual derecursion 10,000", [](nonius::chronometer meter){
  int counter = 0;
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  std::function<void(mi::any_time_executor_ref<> exec)> recurse;
  recurse = [&](mi::any_time_executor_ref<> tr) {
    if (--counter <= 0)
      return;
    tr | op::submit(recurse);
  };
  meter.measure([&]{
    counter = 10'000;
    return tr | op::submit([&](auto exec) { recurse(exec); });
  });
})

NONIUS_BENCHMARK("trampoline static derecursion 10,000", [](nonius::chronometer meter){
  int counter = 0;
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  countdownsingle single{counter};
  meter.measure([&]{
    counter = 10'000;
    return tr | op::submit(single);
  });
})

NONIUS_BENCHMARK("new thread 10 blocking_submits", [](nonius::chronometer meter){
  auto nt = mi::new_thread();
  using NT = decltype(nt);
  meter.measure([&]{
    return nt |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::transform([](auto nt){
        return v::now(nt);
      }) |
      op::get<std::chrono::system_clock::time_point>;
  });
})

NONIUS_BENCHMARK("pool 10 blocking_submits", [](nonius::chronometer meter){
  mi::pool pl{std::max(1u,std::thread::hardware_concurrency())};
  auto pe = pl.executor();
  using PE = decltype(pe);
  meter.measure([&]{
    return pe |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::blocking_submit() |
      op::transform([](auto pe){
        return mi::now(pe);
      }) |
      op::get<std::chrono::system_clock::time_point>;
  });
})
