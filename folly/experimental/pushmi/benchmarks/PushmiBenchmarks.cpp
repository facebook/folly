
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
  void operator()(ExecutorRef exec) const;
};

template <class ExecutorRef>
void countdownsingle::operator()(ExecutorRef exec) const {
  if (--*counter > 0) {
    //exec | op::submit(mi::make_single(*this));
    exec | op::submit(mi::single<countdownsingle, mi::abortEF, mi::ignoreDF>{*this});
  }
}

struct inline_executor {
    using properties = mi::property_set<mi::is_sender<>, mi::is_single<>>;
    template<class Out>
    void submit(Out out) {
      ::mi::set_value(out, *this);
    }
};


#define concept Concept
#include <nonius/nonius.h++>

NONIUS_BENCHMARK("inline 10", [](nonius::chronometer meter){
  int counter = 0;
  auto ie = inline_executor{};
  using IE = decltype(ie);
  countdownsingle single{counter};
  meter.measure([&]{
    counter = 10;
    ie | op::submit(mi::make_single(single));
    return counter;
  });
})

NONIUS_BENCHMARK("trampoline static derecursion 10", [](nonius::chronometer meter){
  int counter = 0;
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  countdownsingle single{counter};
  meter.measure([&]{
    counter = 10;
    tr | op::submit(single);
    return counter;
  });
})

NONIUS_BENCHMARK("trampoline virtual derecursion 10", [](nonius::chronometer meter){
  int counter = 0;
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  std::function<void(mi::any_time_executor_ref<> exec)> recurse{countdownsingle{counter}};
  meter.measure([&]{
    counter = 10;
    tr | op::submit([&](auto exec) { recurse(exec); });
    return counter;
  });
})

NONIUS_BENCHMARK("pool{1} blocking_submit 10", [](nonius::chronometer meter){
  mi::pool pl{std::max(1u,std::thread::hardware_concurrency())};
  auto pe = pl.executor();
  using PE = decltype(pe);
  int counter = 0;
  countdownsingle single{counter};
  meter.measure([&]{
    counter = 10;
    pe | op::blocking_submit(single);
    return counter;
  });
})

NONIUS_BENCHMARK("new thread blocking_submit 10", [](nonius::chronometer meter){
  auto nt = mi::new_thread();
  using NT = decltype(nt);
  int counter = 0;
  countdownsingle single{counter};
  meter.measure([&]{
    counter = 10;
    nt | op::blocking_submit(single);
    return counter;
  });
})
