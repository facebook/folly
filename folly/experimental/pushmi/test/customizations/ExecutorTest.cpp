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

#include <folly/experimental/pushmi/customizations/Executor.h>
#include <folly/experimental/pushmi/customizations/EventBase.h>
#include <folly/experimental/pushmi/customizations/InlineExecutor.h>
#include <folly/experimental/pushmi/customizations/QueuedImmediateExecutor.h>
#include <folly/experimental/pushmi/o/for_each.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/on.h>
#include <folly/experimental/pushmi/o/schedule.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/via.h>
#include <folly/portability/GTest.h>

using namespace folly;

struct ReceiverRecorder {
  using receiver_category = folly::pushmi::receiver_tag;

  template <class... VN>
  void value(VN&&...) {
    ++values;
  }
  template <class E>
  void error(E&&) noexcept {
    ++errors;
  }
  void done() {
    ++dones;
  }
  int values{0};
  int errors{0};
  int dones{0};
};

constexpr class Recorder {
  template <class Out>
  struct receiver {
    using receiver_category = folly::pushmi::receiver_category_t<Out>;
    ReceiverRecorder* r_;
    std::decay_t<Out> out_;
    template <class... VN>
    void value(VN&&... vn) {
      ++r_->values;
      folly::pushmi::set_value(out_, (VN &&) vn...);
    }
    template <class E>
    void error(E&& e) noexcept {
      ++r_->errors;
      folly::pushmi::set_error(out_, (E &&) e);
    }
    void done() {
      ++r_->dones;
      folly::pushmi::set_done(out_);
    }
  };
  template <class In>
  struct sender {
    ReceiverRecorder* r_;
    std::decay_t<In> in_;
    using sender_category = folly::pushmi::sender_category_t<In>;
    using properties =
        folly::pushmi::property_set<folly::pushmi::is_maybe_blocking<>>;
    template <class Out>
    void submit(Out&& out) & {
      folly::pushmi::submit(in_, receiver<Out>{r_, (Out &&) out});
    }
    template <class Out>
    void submit(Out&& out) && {
      folly::pushmi::submit(std::move(in_), receiver<Out>{r_, (Out &&) out});
    }
  };
  struct adapt_fn {
    ReceiverRecorder* r_;
    template <class In>
    auto operator()(In&& in) {
      return sender<In>{r_, (In &&) in};
    }
  };

 public:
  auto operator()(ReceiverRecorder* r) const {
    return adapt_fn{r};
  }
} recorder;

struct FlowReceiverRecorder {
  using receiver_category = folly::pushmi::flow_receiver_tag;

  template <class... VN>
  void value(VN&&...) {
    ++values;
  }
  template <class E>
  void error(E&&) noexcept {
    ++errors;
  }
  void done() {
    ++dones;
  }
  template <class Up>
  void starting(Up&&) noexcept {
    ++startings;
  }
  int values{0};
  int errors{0};
  int dones{0};
  int startings{0};
};

constexpr class FlowRecorder {
  template <class Out>
  struct receiver {
    using receiver_category = folly::pushmi::receiver_category_t<Out>;
    FlowReceiverRecorder* r_;
    std::decay_t<Out> out_;
    template <class... VN>
    void value(VN&&... vn) {
      ++r_->values;
      folly::pushmi::set_value(out_, (VN &&) vn...);
    }
    template <class E>
    void error(E&& e) noexcept {
      ++r_->errors;
      folly::pushmi::set_error(out_, (E &&) e);
    }
    void done() {
      ++r_->dones;
      folly::pushmi::set_done(out_);
    }
    template <class Up>
    void starting(Up&& up) noexcept {
      ++r_->startings;
      folly::pushmi::set_starting(out_, (Up &&) up);
    }
  };
  template <class In>
  struct sender {
    FlowReceiverRecorder* r_;
    std::decay_t<In> in_;
    using sender_category = folly::pushmi::sender_category_t<In>;
    using properties = folly::pushmi::properties_t<In>;
    template <class Out>
    void submit(Out&& out) & {
      folly::pushmi::submit(in_, receiver<Out>{r_, (Out &&) out});
    }
    template <class Out>
    void submit(Out&& out) && {
      folly::pushmi::submit(std::move(in_), receiver<Out>{r_, (Out &&) out});
    }
  };
  struct adapt_fn {
    FlowReceiverRecorder* r_;
    template <class In>
    auto operator()(In&& in) {
      return sender<In>{r_, (In &&) in};
    }
  };

 public:
  auto operator()(FlowReceiverRecorder* r) const {
    return adapt_fn{r};
  }
} flow_recorder;

TEST(Executor, ScheduleInlineExecutor) {
  InlineExecutor x;
  auto xt = getKeepAliveToken(x);
  ReceiverRecorder r{};
  auto s = xt | folly::pushmi::operators::schedule() | recorder(&r);
  EXPECT_EQ(r.values, 0);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 0);
  folly::pushmi::submit(std::move(s), folly::pushmi::receiver<>{});
  EXPECT_EQ(r.values, 1);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 1);
}

TEST(Executor, ComposeInlineExecutor) {
  InlineExecutor x;
  auto xt = getKeepAliveToken(x);
  ReceiverRecorder r{};
  EXPECT_EQ(r.values, 0);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 0);
  xt | folly::pushmi::operators::schedule() | recorder(&r) |
      folly::pushmi::operators::submit();
  EXPECT_EQ(r.values, 1);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 1);
}

TEST(Executor, ViaInlineExecutor) {
  InlineExecutor x;
  auto xt = getKeepAliveToken(x);
  ReceiverRecorder r{};
  EXPECT_EQ(r.values, 0);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 0);
  xt | folly::pushmi::operators::schedule() | recorder(&r) |
      folly::pushmi::operators::via([xt]() { return xt; }) | recorder(&r) |
      folly::pushmi::operators::submit();
  EXPECT_EQ(r.values, 2);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 2);
}

TEST(Executor, FlowViaOnEventBase) {
  folly::EventBase evb;
  folly::EventBase evb1;
  std::thread t([&] { evb.loopForever(); });
  std::thread t1([&] { evb1.loopForever(); });

  std::array<std::string, 3> arr{{"42", "43", "44"}};
  auto m = folly::pushmi::operators::flow_from(arr);

  FlowReceiverRecorder r{};
  EXPECT_EQ(r.values, 0);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 0);
  EXPECT_EQ(r.startings, 0);

  m |
      folly::pushmi::operators::transform( //
          [&](auto s) { return s + "a"; }) |
      folly::pushmi::operators::via( //
          [&]() { return getKeepAliveToken(&evb1); }) |
      flow_recorder(&r) |
      folly::pushmi::operators::on( //
          [&]() { return getKeepAliveToken(&evb); }) |
      flow_recorder(&r) |
      folly::pushmi::operators::for_each( //
          [](auto) {},
          [&](auto) noexcept {
            evb.terminateLoopSoon();
            evb1.terminateLoopSoon();
          },
          [&]() {
            evb.terminateLoopSoon();
            evb1.terminateLoopSoon();
          });

  t.join();
  t1.join();

  EXPECT_EQ(r.values, 6);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 2);
  EXPECT_EQ(r.startings, 2);
}

TEST(Executor, OnEventBase) {
  folly::EventBase evb;
  std::array<int, 3> arr{{0, 9, 99}};
  auto m = folly::pushmi::operators::from(arr);

  m |
      folly::pushmi::operators::on(
          [&]() -> Executor::KeepAlive<SequencedExecutor> {
            return getKeepAliveToken(&evb);
          }) |
      folly::pushmi::operators::submit(
          [&](auto) { EXPECT_EQ(evb.isInEventBaseThread(), true); });
  evb.loop();
}

TEST(Executor, ScheduleAtEventBase) {
  folly::EventBase evb(true);
  auto evbt = getKeepAliveToken(evb);
  ReceiverRecorder r{};

  auto s =
      evbt | folly::pushmi::operators::schedule_at(folly::pushmi::now(evbt));
  EXPECT_EQ(r.values, 0);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 0);
  std::move(s) | recorder(&r) | folly::pushmi::operators::submit();
  while (r.dones == 0) {
    evb.loopOnce();
  }
  EXPECT_EQ(r.values, 1);
  EXPECT_EQ(r.errors, 0);
  EXPECT_EQ(r.dones, 1);
}
