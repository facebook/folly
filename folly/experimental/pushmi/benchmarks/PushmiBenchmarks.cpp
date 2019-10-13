/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <numeric>
#include <thread>
#include <vector>

#include <folly/experimental/pushmi/o/defer.h>
#include <folly/experimental/pushmi/o/for_each.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/on.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/via.h>

#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/time_source.h>
#include <folly/experimental/pushmi/executor/trampoline.h>

#include <folly/experimental/pushmi/entangle.h>
#include <folly/experimental/pushmi/receiver/receiver.h>
#include <folly/experimental/pushmi/executor/properties.h>
#include <folly/experimental/pushmi/sender/properties.h>

#include <folly/experimental/pushmi/examples/pool.h>

#include <folly/Benchmark.h>
#include <folly/Optional.h>
#include <folly/container/Foreach.h>

using namespace folly::pushmi::aliases;

template <class R>
struct countdown {
  explicit countdown(std::atomic<int>& c) : counter(&c) {}

  using receiver_category = mi::receiver_category_t<folly::invoke_result_t<R>>;

  std::atomic<int>* counter;

  template <class ExecutorRef>
  void value(ExecutorRef exec);
  template <class E>
  void error(E) {
    std::terminate();
  }
  void done() {}
  PUSHMI_TEMPLATE(class Up)
  (requires mi::ReceiveValue<Up, std::ptrdiff_t>) //
  void starting(Up up) {
    mi::set_value(up, 1);
  }
  PUSHMI_TEMPLATE(class Up)
  (requires mi::ReceiveValue<Up>&& (!mi::ReceiveValue<Up, std::ptrdiff_t>)) //
  void starting(Up up) {
    mi::set_value(up);
  }
};

template <class R>
template <class ExecutorRef>
void countdown<R>::value(ExecutorRef exec) {
  if (--*counter >= 0) {
    exec.schedule() | op::submit(R{}(*this));
  }
}

using countdownsingle = countdown<decltype(mi::make_receiver)>;
using countdownflowsingle = countdown<decltype(mi::make_flow_receiver)>;
using countdownmany = countdown<decltype(mi::make_receiver)>;
using countdownflowmany = countdown<decltype(mi::make_flow_receiver)>;

struct inline_time_executor {
  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::single_sender_tag::with_values<inline_time_executor>::no_error {
  private:
    std::chrono::system_clock::time_point at_;
  public:
    using properties = mi::property_set<mi::is_always_blocking<>>;

    task() = default;
    explicit task(std::chrono::system_clock::time_point at) : at_(at) {}

    template <class Out>
    void submit(Out out) {
      std::this_thread::sleep_until(at_);
      ::mi::set_value(out, inline_time_executor{});
      ::mi::set_done(out);
    }
  };

  std::chrono::system_clock::time_point top() {
    return std::chrono::system_clock::now();
  }
  auto schedule(std::chrono::system_clock::time_point at) {
    return task{at};
  }
  auto schedule() {
    return schedule(top());
  }
};

struct inline_executor {
  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task : mi::single_sender_tag::with_values<inline_executor>::no_error {
    using properties = mi::property_set<mi::is_always_blocking<>>;

    template <class Out>
    void submit(Out out) {
      ::mi::set_value(out, inline_executor{});
      ::mi::set_done(out);
    }
  };
  auto schedule() {
    return task{};
  }
};

template <class CancellationFactory>
struct inline_executor_flow_single {
  CancellationFactory cf;

  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::flow_single_sender_tag::with_values<inline_executor_flow_single>
      ::no_error {
  private:
    CancellationFactory cf_;

  public:
    using properties = mi::property_set<mi::is_maybe_blocking<>>;

    task() = default;
    explicit task(CancellationFactory cf) : cf_(std::move(cf)) {}

    template <class Out>
    void submit(Out out) {
      auto tokens = cf_();

      using Stopper = decltype(tokens.second);
      struct Data : mi::receiver<> {
        explicit Data(Stopper s) : stopper(std::move(s)) {}
        Stopper stopper;
      };
      auto up = mi::make_receiver(
          Data{std::move(tokens.second)},
          [](auto&) {},
          [](auto& data, auto) noexcept {
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          },
          [](auto& data) {
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          });

      // pass reference for cancellation.
      ::mi::set_starting(out, std::move(up));

      auto both = lock_both(tokens.first);
      if (!!both.first && !*(both.first)) {
        ::mi::set_value(out, inline_executor_flow_single{cf_});
        ::mi::set_done(out);
      } else {
        // cancellation is not an error
        ::mi::set_done(out);
      }
    }
  };

  auto schedule() {
    return task{cf};
  }
};

struct shared_cancellation_factory {
  auto operator()() {
    // boolean cancellation
    bool stop = false;
    auto set_stop = [](auto& stop_) {
      if (!!stop_) {
        *stop_ = true;
      }
    };
    return mi::shared_entangle(stop, set_stop);
  }
};
using inline_executor_flow_single_shared =
    inline_executor_flow_single<shared_cancellation_factory>;

struct entangled_cancellation_factory {
  auto operator()() {
    // boolean cancellation
    bool stop = false;
    auto set_stop = [](auto& stop_) {
      if (!!stop_) {
        *stop_ = true;
      }
    };
    return mi::entangle(stop, set_stop);
  }
};
using inline_executor_flow_single_entangled =
    inline_executor_flow_single<entangled_cancellation_factory>;

struct inline_executor_flow_single_ignore {
  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::flow_single_sender_tag::with_values<inline_executor_flow_single_ignore>
      ::no_error {
    using properties = mi::property_set<mi::is_maybe_blocking<>>;

    template <class Out>
    void submit(Out out) {
      // pass reference for cancellation.
      ::mi::set_starting(out, mi::receiver<>{});
      ::mi::set_value(out, inline_executor_flow_single_ignore{});
      ::mi::set_done(out);
    }
  };

  auto schedule() {
    return task{};
  }
};

struct inline_executor_flow_many {
  inline_executor_flow_many() = default;
  inline_executor_flow_many(std::atomic<int>& c) : counter(&c) {}

  std::atomic<int>* counter = nullptr;

  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::flow_sender_tag::with_values<inline_executor_flow_many>
      ::no_error {
  private:
    std::atomic<int>* counter = nullptr;

  public:
    using properties = mi::property_set<mi::is_maybe_blocking<>>;
    task() = default;
    explicit task(std::atomic<int>* c) : counter(c) {}

    template <class Out>
    void submit(Out out) {
      // boolean cancellation
      struct producer {
        producer(Out o, bool s) : out(std::move(o)), stop(s) {}
        Out out;
        std::atomic<bool> stop;
      };
      auto p = std::make_shared<producer>(std::move(out), false);

      struct Data : mi::receiver<> {
        explicit Data(std::shared_ptr<producer> sp) : p(std::move(sp)) {}
        std::shared_ptr<producer> p;
      };

      auto up = mi::make_receiver(
          Data{p},
          [counter = this->counter](auto& data, auto requested) {
            if (requested < 1) {
              return;
            }
            // this is re-entrant
            while (!data.p->stop && --requested >= 0 &&
                  (!counter || --*counter >= 0)) {
              ::mi::set_value(
                  data.p->out,
                  !!counter ? inline_executor_flow_many{*counter}
                            : inline_executor_flow_many{});
            }
            if (!counter || *counter == 0) {
              ::mi::set_done(data.p->out);
            }
          },
          [](auto& data, auto) noexcept {
            data.p->stop.store(true);
            ::mi::set_done(data.p->out);
          },
          [](auto& data) {
            data.p->stop.store(true);
            ::mi::set_done(data.p->out);
          });

      // pass reference for cancellation.
      ::mi::set_starting(p->out, std::move(up));
    }
  };

  auto schedule() {
    return task{counter};
  }
};

struct inline_executor_flow_many_ignore {
  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::flow_sender_tag::with_values<inline_executor_flow_many_ignore>
      ::no_error {
    using properties = mi::property_set<mi::is_always_blocking<>>;
    template <class Out>
    void submit(Out out) {
      // pass reference for cancellation.
      ::mi::set_starting(out, mi::receiver<>{});
      ::mi::set_value(out, inline_executor_flow_many_ignore{});
      ::mi::set_done(out);
    }
  };

  auto schedule() {
    return task{};
  }
};

struct inline_executor_many {
  using properties = mi::property_set<mi::is_fifo_sequence<>>;

  struct task
  : mi::sender_tag::with_values<inline_executor_many>::no_error {
    using properties = mi::property_set<mi::is_always_blocking<>>;
    template <class Out>
    void submit(Out out) {
      ::mi::set_value(out, inline_executor_many{});
      ::mi::set_done(out);
    }
  };

  auto schedule() {
    return task{};
  }
};

BENCHMARK(ready_1000_single_get_submit) {
  int counter{0};
  counter = 1'000;
  std::atomic<int> result{0};
  while (--counter >=0) {
    result += op::just(42) | op::get<int>;
  }
  (void) result.load();
}

BENCHMARK(ready_1000_single_get_blocking_submit) {
  int counter{0};
  counter = 1'000;
  std::atomic<int> result{0};
  while (--counter >=0) {
    result += mi::make_single_sender(
      [](auto out){
        mi::set_value(out, 42);
        mi::set_done(out);
      })
    | op::get<int>;
  }
  (void) result.load();
}

BENCHMARK(inline_1000_single, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor{};
  using IE = decltype(ie);
  countdownsingle single{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_receiver(single));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_time_single, n) {
  std::atomic<int> counter{0};
  auto ie = inline_time_executor{};
  using IE = decltype(ie);
  countdownsingle single{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_receiver(single));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_many, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_many{};
  using IE = decltype(ie);
  countdownmany many{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_receiver(many));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_flow_single_shared, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_single_shared{};
  using IE = decltype(ie);
  countdownflowsingle flowsingle{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_flow_receiver(flowsingle));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_flow_single_entangle, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_single_entangled{};
  using IE = decltype(ie);
  countdownflowsingle flowsingle{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_flow_receiver(flowsingle));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_flow_single_ignore_cancellation, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_single_ignore{};
  using IE = decltype(ie);
  countdownflowsingle flowsingle{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_flow_receiver(flowsingle));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_flow_many, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_many{};
  using IE = decltype(ie);
  countdownflowmany flowmany{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_flow_receiver(flowmany));
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1_flow_many_with_1000_values_pull_1, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_many{counter};
  using IE = decltype(ie);

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::for_each(mi::make_receiver());
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1_flow_many_with_1000_values_pull_1000, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_many{counter};
  using IE = decltype(ie);

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule()
      | op::submit(
          mi::make_flow_receiver(
            mi::ignoreNF{},
            mi::abortEF{},
            mi::ignoreDF{},
            [](auto up){
              mi::set_value(up, 1'000);
            }
          )
        );
    while(counter.load() > 0);
  }
}

BENCHMARK(inline_1000_flow_many_ignore_cancellation, n) {
  std::atomic<int> counter{0};
  auto ie = inline_executor_flow_many_ignore{};
  using IE = decltype(ie);
  countdownflowmany flowmany{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    ie.schedule() | op::submit(mi::make_flow_receiver(flowmany));
    while(counter.load() > 0);
  }
}

BENCHMARK(trampoline_1000_single_get_blocking_submit, n) {
  int counter{0};
  auto tr = mi::trampoline();
  using TR = decltype(tr);

  FOR_EACH_RANGE (i, 0, n) {
    counter = 1'000;
    while (--counter >= 0) {
      auto fortyTwo = tr.schedule()
        | op::transform([](auto){return 42;})
        | op::get<int>;
      (void) fortyTwo;
    }
    (void) counter;
  }
}

BENCHMARK(trampoline_static_derecursion_1000, n) {
  std::atomic<int> counter{0};
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  countdownsingle single{counter};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    tr.schedule() | op::submit(single);
    while(counter.load() > 0);
  }
}

BENCHMARK(trampoline_virtual_derecursion_1000, n) {
  std::atomic<int> counter{0};
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  auto single = countdownsingle{counter};
  std::function<void(mi::any_executor_ref<>)> recurse{
      [&](auto exec) { mi::set_value(single, exec); }};

  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    tr.schedule() | op::submit([&](auto exec) { recurse(exec); });
    while(counter.load() > 0);
  }
}

BENCHMARK(trampoline_flow_many_sender_1000, n) {
  std::atomic<int> counter{0};
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  std::vector<int> values(1'000);
  BENCHMARK_SUSPEND {
    std::iota(values.begin(), values.end(), 1);
  }
  auto f = op::flow_from(values, [&]{return tr;}) | op::tap([&](int){
    --counter;
  });
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    f | op::for_each(mi::make_receiver());
    while(counter.load() > 0);
  }
}

BENCHMARK(pool_1_submit_1000, n) {
  folly::Optional<mi::pool> pl;
  mi::pool_executor pe;
  BENCHMARK_SUSPEND {
    pl.emplace(1u);
    pe = pl->executor();
  }
  std::atomic<int> counter{0};
  countdownsingle single{counter};
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    pe.schedule() | op::submit(single);
    while(counter.load() > 0);
  }
}

BENCHMARK(pool_hardware_concurrency_submit_1000, n) {
  folly::Optional<mi::pool> pl;
  mi::pool_executor pe;
  BENCHMARK_SUSPEND {
    pl.emplace(std::max(1u, std::thread::hardware_concurrency()));
    pe = pl->executor();
  }
  std::atomic<int> counter{0};
  countdownsingle single{counter};
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    pe.schedule() | op::submit(single);
    while(counter.load() > 0);
  }
}

BENCHMARK(new_thread_submit_1000, n) {
  auto nt = mi::new_thread();
  using NT = decltype(nt);
  std::atomic<int> counter{0};
  countdownsingle single{counter};
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    nt.schedule() | op::submit(single);
    while(counter.load() > 0);
  }
}

BENCHMARK(new_thread_blocking_submit_1000, n) {
  auto nt = mi::new_thread();
  std::atomic<int> counter{0};
  countdownsingle single{counter};
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    nt.schedule() | op::blocking_submit(single);
  }
}

BENCHMARK(new_thread_and_time_submit_1000, n) {
  auto nt = mi::new_thread();
  auto time = mi::time_source<>{};
  auto strands = time.make(mi::systemNowF{}, nt);
  auto tnt = mi::make_strand(strands);
  std::atomic<int> counter{0};
  countdownsingle single{counter};
  FOR_EACH_RANGE (i, 0, n) {
    counter.store(1'000);
    tnt.schedule() | op::submit(single);
    while(counter.load() > 0);
  }
  BENCHMARK_SUSPEND {
    time.join();
  }
}

int main() {
  folly::runBenchmarks();
}
