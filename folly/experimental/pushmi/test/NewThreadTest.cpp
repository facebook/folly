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

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include <folly/experimental/pushmi/sender/flow_single_sender.h>
#include <folly/experimental/pushmi/o/empty.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/on.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/schedule.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/via.h>

#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/strand.h>
#include <folly/experimental/pushmi/executor/time_source.h>

using namespace folly::pushmi::aliases;

#include <folly/Conv.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

struct countdownsingle {
  explicit countdownsingle(int& c) : counter(&c) {}

  int* counter;

  template <class ExecutorRef>
  void operator()(ExecutorRef exec) {
    if (--*counter > 0) {
      exec | op::schedule() | op::submit(*this);
    }
  }
};

using NT = decltype(mi::new_thread());

inline auto make_time(mi::time_source<>& t, NT& ex) {
  auto strands = t.make(mi::systemNowF{}, ex);
  return mi::make_strand(strands);
}

class NewthreadExecutor : public Test {
 public:
  ~NewthreadExecutor() override {
    time_.join();
  }

 protected:
  using TNT = mi::invoke_result_t<decltype(make_time), mi::time_source<>&, NT&>;

  NT nt_{mi::new_thread()};
  mi::time_source<> time_{};
  TNT tnt_{make_time(time_, nt_)};
};

TEST_F(NewthreadExecutor, BlockingSubmitNow) {
  auto signals = 0;
  auto start = v::now(tnt_);
  auto signaled = start;
  tnt_ | op::schedule() | op::transform([](auto tnt) { return tnt | ep::now(); }) |
      op::blocking_submit(
          [&](auto at) {
            signaled = at;
            signals += 100;
          },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(110))
      << "expected that the value and done signals are recorded once and the value signal did not drift much";
  auto delay =
      std::chrono::duration_cast<std::chrono::milliseconds>((signaled - start))
          .count();
  EXPECT_THAT(delay, Lt(1000)) << "The delay is " << delay;
}

TEST_F(NewthreadExecutor, BlockingGetNow) {
  auto start = v::now(tnt_);
  auto signaled = tnt_ | op::schedule() | op::transform([](auto tnt) { return v::now(tnt); }) |
      op::get<std::chrono::system_clock::time_point>;

  auto delay =
      std::chrono::duration_cast<std::chrono::milliseconds>((signaled - start))
          .count();

  EXPECT_THAT(delay, Lt(1000)) << "The delay is " << delay;
}

TEST_F(NewthreadExecutor, SubmissionsAreOrderedInTime) {
  std::vector<std::string> times;
  std::atomic<int> pushed{0};
  auto push = [&](int time) {
    return v::on_value([&, time](auto) {
      times.push_back(folly::to<std::string>(time));
      ++pushed;
    });
  };
  tnt_ | op::schedule() | op::submit(v::on_value([push](auto tnt) {
    auto now = tnt | ep::now();
    tnt | op::schedule_after(40ms) | op::submit(push(40));
    tnt | op::schedule_at(now + 10ms) | op::submit(push(10));
    tnt | op::schedule_after(20ms) | op::submit(push(20));
    tnt | op::schedule_at(now + 10ms) | op::submit(push(11));
  }));

  while (pushed.load() < 4) {
    std::this_thread::yield();
  }

  EXPECT_THAT(times, ElementsAre("10", "11", "20", "40"))
      << "expected that the items were pushed in time order not insertion order";
}

TEST_F(NewthreadExecutor, NowIsCalled) {
  bool done = false;
  tnt_ | ep::now();
  tnt_ | op::schedule() | op::blocking_submit([&](auto tnt) {
    tnt | ep::now();
    done = true;
  });

  EXPECT_THAT(done, Eq(true)) << "exptected that both calls to now() complete";
}

TEST_F(NewthreadExecutor, BlockingSubmit) {
  auto signals = 0;
  nt_ | op::schedule() | op::transform([](auto) { return 42; }) |
      op::blocking_submit(
          [&](auto) { signals += 100; },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(110))
      << "the value and done signals are recorded once";
}

TEST_F(NewthreadExecutor, BlockingGet) {
  auto v = nt_ | op::schedule() | op::transform([](auto) { return 42; }) | op::get<int>;

  EXPECT_THAT(v, Eq(42)) << "expected that the result would be different";
}

TEST_F(NewthreadExecutor, VirtualDerecursion) {
  int counter = 100'000;
  std::function<void(::folly::pushmi::any_executor_ref<> exec)> recurse;
  recurse = [&](::folly::pushmi::any_executor_ref<> nt) {
    if (--counter <= 0)
      return;
    nt | op::schedule() | op::submit(recurse);
  };
  nt_ | op::schedule() | op::blocking_submit([&](auto nt) { recurse(nt); });

  EXPECT_THAT(counter, Eq(0))
      << "expected that all nested submissions complete";
}

TEST_F(NewthreadExecutor, StaticDerecursion) {
  int counter = 100'000;
  countdownsingle single{counter};
  nt_ | op::schedule() | op::blocking_submit(single);

  EXPECT_THAT(counter, Eq(0))
      << "expected that all nested submissions complete";
}

TEST_F(NewthreadExecutor, UsedWithOn) {
  std::vector<std::string> values;
  auto sender = ::folly::pushmi::make_single_sender([](auto out) {
    ::folly::pushmi::set_value(out, 2.0);
    ::folly::pushmi::set_done(out);
    // ignored
    ::folly::pushmi::set_value(out, 1);
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
  });
  sender | op::on(mi::strands(nt_)) |
      op::blocking_submit(v::on_value(
          [&](auto v) { values.push_back(folly::to<std::string>(v)); }));

  EXPECT_THAT(values, ElementsAre(folly::to<std::string>(2.0)))
      << "expected that only the first item was pushed";
}

TEST_F(NewthreadExecutor, UsedWithVia) {
  std::vector<std::string> values;
  auto sender = ::folly::pushmi::make_single_sender([](auto out) {
    ::folly::pushmi::set_value(out, 2.0);
    ::folly::pushmi::set_done(out);
    // ignored
    ::folly::pushmi::set_value(out, 1);
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
  });
  sender | op::via(mi::strands(nt_)) |
      op::blocking_submit(v::on_value(
          [&](auto v) { values.push_back(folly::to<std::string>(v)); }));

  EXPECT_THAT(values, ElementsAre(folly::to<std::string>(2.0)))
      << "expected that only the first item was pushed";
}
