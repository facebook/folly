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
#include <folly/experimental/pushmi/o/schedule.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/via.h>

#include <folly/experimental/pushmi/executor/inline.h>
#include <folly/experimental/pushmi/executor/trampoline.h>

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

using TR = decltype(mi::trampoline());

class TrampolineExecutor : public Test {
 protected:
  TR tr_{mi::trampoline()};
};

TEST_F(TrampolineExecutor, TransformAndSubmit) {
  auto signals = 0;
  tr_ | op::schedule() | op::transform([](auto) { return 42; }) |
      op::submit(
          [&](auto) { signals += 100; },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(110))
      << "expected that the value and done signals are each recorded once";
}

TEST_F(TrampolineExecutor, BlockingGet) {
  auto v = tr_ | op::schedule() | op::transform([](auto) { return 42; }) | op::get<int>;

  EXPECT_THAT(v, Eq(42)) << "expected that the result would be different";
}

TEST_F(TrampolineExecutor, VirtualDerecursion) {
  int counter = 100'000;
  std::function<void(::folly::pushmi::any_executor_ref<> exec)> recurse;
  recurse = [&](::folly::pushmi::any_executor_ref<> tr) {
    if (--counter <= 0)
      return;
    tr | op::schedule() | op::submit(recurse);
  };
  tr_ | op::schedule() | op::submit([&](auto exec) { recurse(exec); });

  EXPECT_THAT(counter, Eq(0))
      << "expected that all nested submissions complete";
}

TEST_F(TrampolineExecutor, StaticDerecursion) {
  int counter = 100'000;
  countdownsingle single{counter};
  tr_ | op::schedule() | op::submit(single);

  EXPECT_THAT(counter, Eq(0))
      << "expected that all nested submissions complete";
}

TEST_F(TrampolineExecutor, UsedWithOn) {
  std::vector<std::string> values;
  auto sender = ::folly::pushmi::make_single_sender([](auto out) {
    ::folly::pushmi::set_value(out, 2.0);
    ::folly::pushmi::set_done(out);
    // ignored
    ::folly::pushmi::set_value(out, 1);
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
  });
  auto inlineon = sender | op::on([&]() { return mi::inline_executor(); });
  inlineon | op::submit(v::on_value([&](auto v) {
    values.push_back(folly::to<std::string>(v));
  }));

  EXPECT_THAT(values, ElementsAre(folly::to<std::string>(2.0)))
      << "expected that only the first item was pushed";
}

TEST_F(TrampolineExecutor, UsedWithVia) {
  std::vector<std::string> values;
  auto sender = ::folly::pushmi::make_single_sender([](auto out) {
    ::folly::pushmi::set_value(out, 2.0);
    ::folly::pushmi::set_done(out);
    // ignored
    ::folly::pushmi::set_value(out, 1);
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
    ::folly::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
  });
  auto inlinevia = sender | op::via([&]() { return mi::inline_executor(); });
  inlinevia | op::submit(v::on_value([&](auto v) {
    values.push_back(folly::to<std::string>(v));
  }));

  EXPECT_THAT(values, ElementsAre(folly::to<std::string>(2.0)))
      << "expected that only the first item was pushed";
}
