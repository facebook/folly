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

#include <folly/experimental/pushmi/sender/flow_sender.h>
#include <folly/experimental/pushmi/o/empty.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/for_each.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/take_until.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/via.h>

#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/strand.h>
#include <folly/experimental/pushmi/executor/trampoline.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

namespace detail {

struct receiver_counters {
  std::atomic<int> values_{0};
  std::atomic<int> errors_{0};
  std::atomic<int> dones_{0};
  std::atomic<int> startings_{0};
  std::atomic<int> finallys_{0};
};

template <class Base = mi::receiver<>>
struct ReceiverSignals_ : Base {
  ~ReceiverSignals_() {}
  ReceiverSignals_(const ReceiverSignals_&) = default;
  ReceiverSignals_& operator=(const ReceiverSignals_&) = default;
  ReceiverSignals_(ReceiverSignals_&&) = default;
  ReceiverSignals_& operator=(ReceiverSignals_&&) = default;
  explicit ReceiverSignals_(std::string id) :
    id_(std::move(id)),
    counters_(std::make_shared<receiver_counters>()) {}
  std::string id_;
  std::shared_ptr<receiver_counters> counters_;

  void value(mi::detail::any) {
    if (mi::FlowReceiver<ReceiverSignals_> != false) {
      EXPECT_THAT(counters_->startings_.load(), Eq(1))
          << "[" << id_
          << "]::value() expected the starting signal to be recorded before the value signal";
    }
    EXPECT_THAT(counters_->finallys_.load(), Eq(0))
        << "[" << id_
        << "]::value() expected the value signal to be recorded before the done/error signal";
    ++counters_->values_;
  }
  void error(mi::detail::any) noexcept {
    if (mi::FlowReceiver<ReceiverSignals_> != false) {
      EXPECT_THAT(counters_->startings_.load(), Eq(1))
          << "[" << id_
          << "]::error() expected the starting signal to be recorded before the error signal";
    }
    EXPECT_THAT(counters_->finallys_.load(), Eq(0))
        << "[" << id_
        << "]::error() expected only one of done/error signals to be recorded";
    ++counters_->errors_;
    ++counters_->finallys_;
  }
  void done() {
    if (mi::FlowReceiver<ReceiverSignals_> != false) {
      EXPECT_THAT(counters_->startings_.load(), Eq(1))
          << "[" << id_
          << "]::done() expected the starting signal to be recorded before the done signal";
    }
    EXPECT_THAT(counters_->finallys_.load(), Eq(0))
        << "[" << id_
        << "]::done() expected only one of done/error signals to be recorded";
    ++counters_->dones_;
    ++counters_->finallys_;
  }
  void starting(mi::detail::any) {
    EXPECT_THAT(counters_->startings_.load(), Eq(0))
        << "[" << id_
        << "]::starting() expected the starting signal to be recorded once";
    ++counters_->startings_;
  }

  void wait() {
    while (counters_->finallys_.load() == 0) {
    }
  }

  template<class Fn>
  void verifyValues(Fn fn) {
    EXPECT_THAT(fn(counters_->values_.load()), Eq(true))
        << "[" << id_
        << "]::verifyValues() expected the value signal(s) to satisfy the predicate.";
  }
  void verifyValues(int count) {
    EXPECT_THAT(counters_->values_.load(), Eq(count))
        << "[" << id_
        << "]::verifyValues() expected the value signal to be recorded ["
        << count << "] times.";
  }
  void verifyErrors() {
    EXPECT_THAT(counters_->errors_.load(), Eq(1))
        << "[" << id_
        << "]::verifyErrors() expected the error signal to be recorded once";
    EXPECT_THAT(counters_->dones_.load(), Eq(0))
        << "[" << id_
        << "]::verifyErrors() expected the dones signal not to be recorded";
    EXPECT_THAT(counters_->finallys_.load(), Eq(1))
        << "[" << id_
        << "]::verifyErrors() expected the finally signal to be recorded once";
  }
  void verifyDones() {
    EXPECT_THAT(counters_->dones_.load(), Eq(1))
        << "[" << id_
        << "]::verifyDones() expected the dones signal to be recorded once";
    EXPECT_THAT(counters_->errors_.load(), Eq(0))
        << "[" << id_
        << "]::verifyDones() expected the errors signal not to be recorded";
    EXPECT_THAT(counters_->finallys_.load(), Eq(1))
        << "[" << id_
        << "]::verifyDones() expected the finally signal to be recorded once";
  }
  void verifyFinal() {
    if (mi::FlowReceiver<ReceiverSignals_> == false) {
      EXPECT_THAT(counters_->startings_.load(), Eq(0))
          << "[" << id_
          << "]::verifyFinal() expected the starting signal not to be recorded";
    } else {
      EXPECT_THAT(counters_->startings_.load(), Eq(1))
          << "[" << id_
          << "]::verifyFinal() expected the starting signal to be recorded once";
    }
    EXPECT_THAT(counters_->finallys_.load(), Eq(1))
        << "[" << id_
        << "]::verifyFinal() expected the finally signal to be recorded once";
  }
};

} // namespace detail

using ReceiverSignals =
    detail::ReceiverSignals_<mi::receiver<>>;
using FlowReceiverSignals =
    detail::ReceiverSignals_<mi::flow_receiver<>>;

auto zeroOrOne = [](int count){
  return count == 0 || count == 1;
};

TEST(EmptySourceEmptyTriggerTrampoline, TakeUntil) {
  std::array<int, 0> ae{};
  auto e = op::flow_from(ae, mi::trampolines);

  FlowReceiverSignals source{"source"};
  FlowReceiverSignals trigger{"trigger"};
  ReceiverSignals each{"each"};

  e | op::tap(source) |
      op::take_until(mi::trampolines, e | op::tap(trigger)) |
      op::for_each(each);

  source.wait();
  source.verifyValues(0);
  source.verifyDones();
  source.verifyFinal();

  trigger.wait();
  trigger.verifyValues(0);
  trigger.verifyDones();
  trigger.verifyFinal();

  each.wait();
  each.verifyValues(0);
  each.verifyDones();
  each.verifyFinal();
}

TEST(EmptySourceEmptyTrigger, TakeUntil) {
  auto nt = mi::new_thread();
  std::array<int, 0> ae{};
  auto e = op::flow_from(ae, mi::strands(nt));

  FlowReceiverSignals source{"source"};
  FlowReceiverSignals trigger{"trigger"};
  ReceiverSignals each{"each"};

  e | op::tap(source) |
      op::take_until(mi::strands(nt), e | op::tap(trigger)) |
      op::for_each(each);

  source.wait();
  source.verifyValues(0);
  source.verifyDones();
  source.verifyFinal();

  trigger.wait();
  trigger.verifyValues(0);
  trigger.verifyDones();
  trigger.verifyFinal();

  each.wait();
  each.verifyValues(0);
  each.verifyDones();
  each.verifyFinal();
}

TEST(EmptySourceValueTrigger, TakeUntil) {
  auto nt = mi::new_thread();
  std::array<int, 0> ae{};
  std::array<int, 1> av{{42}};
  auto e = op::flow_from(ae, mi::strands(nt));
  auto v = op::flow_from(av, mi::strands(nt));

  FlowReceiverSignals source{"source"};
  FlowReceiverSignals trigger{"trigger"};
  ReceiverSignals each{"each"};

  e | op::tap(source) |
      op::take_until(mi::strands(nt), v | op::tap(trigger)) |
      op::for_each(each);

  source.wait();
  source.verifyValues(0);
  source.verifyDones();
  source.verifyFinal();

  trigger.wait();
  trigger.verifyValues(zeroOrOne);
  trigger.verifyDones();
  trigger.verifyFinal();

  each.wait();
  each.verifyValues(0);
  each.verifyDones();
  each.verifyFinal();
}

TEST(ValueSourceEmptyTrigger, TakeUntil) {
  auto nt = mi::new_thread();
  std::array<int, 0> ae{};
  std::array<int, 1> av{{42}};
  auto e = op::flow_from(ae, mi::strands(nt));
  auto v = op::flow_from(av, mi::strands(nt));

  FlowReceiverSignals source{"source"};
  FlowReceiverSignals trigger{"trigger"};
  ReceiverSignals each{"each"};

  v | op::tap(source) |
      op::take_until(mi::strands(nt), e | op::tap(trigger)) |
      op::for_each(each);

  source.wait();
  source.verifyValues(zeroOrOne);
  source.verifyDones();
  source.verifyFinal();

  trigger.wait();
  trigger.verifyValues(0);
  trigger.verifyDones();
  trigger.verifyFinal();

  each.wait();
  each.verifyValues(zeroOrOne);
  each.verifyDones();
  each.verifyFinal();
}

TEST(ValueSourceValueTrigger, TakeUntil) {
  auto nt = mi::new_thread();
  std::array<int, 1> av{{42}};
  auto v = op::flow_from(av, mi::strands(nt));

  FlowReceiverSignals source{"source"};
  FlowReceiverSignals trigger{"trigger"};
  ReceiverSignals each{"each"};

  v | op::tap(source) |
      op::take_until(mi::strands(nt), v | op::tap(trigger)) |
      op::for_each(each);

  source.wait();
  source.verifyValues(zeroOrOne);
  source.verifyDones();
  source.verifyFinal();

  trigger.wait();
  trigger.verifyValues(zeroOrOne);
  trigger.verifyDones();
  trigger.verifyFinal();

  each.wait();
  each.verifyValues(zeroOrOne);
  each.verifyDones();
  each.verifyFinal();
}
