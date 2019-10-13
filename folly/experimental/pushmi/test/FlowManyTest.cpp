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

#include <array>

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include <folly/experimental/pushmi/sender/flow_sender.h>
#include <folly/experimental/pushmi/o/for_each.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/schedule.h>

#include <folly/experimental/pushmi/entangle.h>
#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/time_source.h>
#include <folly/experimental/pushmi/executor/trampoline.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
#define MAKE(x) x MAKE_
#define MAKE_(...) \
  { __VA_ARGS__ }
#else
#define MAKE(x) make_##x
#endif

class ImmediateFlowManySender : public Test {
 protected:
  auto make_producer() {
    return mi::MAKE(flow_sender)([&](auto out) {
      using Out = decltype(out);
      struct Data : mi::receiver<> {
        explicit Data(Out out_) : out(std::move(out_)), stop(false) {}
        Out out;
        bool stop;
      };

      auto up = mi::MAKE(receiver)(
          Data{std::move(out)},
          [&](auto& data, auto requested) {
            signals_ += 1000000;
            if (requested < 1) {
              return;
            }
            // check boolean to select signal
            if (!data.stop) {
              ::mi::set_value(data.out, 42);
            }
            ::mi::set_done(data.out);
          },
          [&](auto& data, auto) noexcept {
            signals_ += 100000;
            data.stop = true;
            ::mi::set_done(data.out);
          },
          [&](auto& data) {
            signals_ += 10000;
            data.stop = true;
            ::mi::set_done(data.out);
          });

      // pass reference for cancellation.
      ::mi::set_starting(up.data().out, std::move(up));
    });
  }

  template <class F>
  auto make_consumer(F f) {
    return mi::MAKE(flow_receiver)(
        mi::on_value([&](int) { signals_ += 100; }),
        mi::on_error([&](auto) noexcept { signals_ += 1000; }),
        mi::on_done([&]() { signals_ += 1; }),
        mi::on_starting([&, f](auto up) {
          signals_ += 10;
          f(std::move(up));
        }));
  }

  int signals_{0};
};

TEST(AnyFlowManySender, Construct) {
  std::array<int, 3> arr{{0, 9, 99}};
  auto m = folly::pushmi::operators::flow_from(arr);
  auto any_m = folly::pushmi::any_flow_sender<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(m);
}

TEST_F(ImmediateFlowManySender, EarlyCancellation) {
  make_producer() | op::submit(make_consumer([](auto up) {
    // immediately stop producer
    ::mi::set_done(up);
  }));

  EXPECT_THAT(signals_, Eq(10011))
      << "expected that the starting, up.done and out.done signals are each recorded once";
}

TEST_F(ImmediateFlowManySender, LateCancellation) {
  make_producer() | op::submit(make_consumer([](auto up) {
    // do not stop producer before it is scheduled to run
    ::mi::set_value(up, 1);
  }));

  EXPECT_THAT(signals_, Eq(1000111))
      << "expected that the starting, up.value, value and done signals are each recorded once";
}

using NT = decltype(mi::new_thread());

inline auto make_time(mi::time_source<>& t, NT& ex) {
  auto strands = t.make(mi::systemNowF{}, ex);
  return mi::make_strand(strands);
}

class ConcurrentFlowManySender : public Test {
 protected:
  using TNT = mi::invoke_result_t<decltype(make_time), mi::time_source<>&, NT&>;

  void reset() {
    at_ = mi::now(tnt_) + 100ms;
    signals_ = 0;
    terminal_ = 0;
    cancel_ = 0;
  }

  void join() {
    timeproduce_.join();
    timecancel_.join();
  }

  void cancellation_test(std::chrono::system_clock::time_point at) {
    auto f = mi::MAKE(flow_sender)([&](auto out) {
      using Out = decltype(out);

      // boolean cancellation
      struct producer {
        producer(Out out_, TNT tnt_, bool s)
            : out(std::move(out_)), tnt(std::move(tnt_)), stop(s) {}
        Out out;
        TNT tnt;
        std::atomic<bool> stop;
      };
      auto p = std::make_shared<producer>(std::move(out), tnt_, false);

      struct Data : mi::receiver<> {
        explicit Data(std::shared_ptr<producer> p_) : p(std::move(p_)) {}
        std::shared_ptr<producer> p;
      };

      auto up = mi::MAKE(receiver)(
          Data{p},
          [&](auto& data, auto requested) {
            signals_ += 1000000;
            if (requested < 1) {
              return;
            }
            // submit work to happen later
            data.p->tnt |
              op::schedule_at(at_) |
              op::submit([p = data.p](auto) {
              // check boolean to select signal
              if (!p->stop) {
                ::mi::set_value(p->out, 42);
                ::mi::set_done(p->out);
              }
            });
          },
          [&](auto& data, auto) noexcept {
            signals_ += 100000;
            data.p->stop.store(true);
            data.p->tnt |
                op::schedule() |
                op::submit([p = data.p](auto) { ::mi::set_done(p->out); });
            ++cancel_;
          },
          [&](auto& data) {
            signals_ += 10000;
            data.p->stop.store(true);
            data.p->tnt |
                op::schedule() |
                op::submit([p = data.p](auto) { ::mi::set_done(p->out); });
            ++cancel_;
          });

      tnt_ |
        op::schedule() |
        op::submit([p, sup = std::move(up)](auto) mutable {
        // pass reference for cancellation.
        ::mi::set_starting(p->out, std::move(sup));
      });
    });
    f |
        op::submit(mi::MAKE(flow_receiver)(
            mi::on_value([&](int) { signals_ += 100; }),
            mi::on_error([&](auto) noexcept {
              signals_ += 1000;
              ++terminal_;
            }),
            mi::on_done([&]() {
              signals_ += 1;
              ++terminal_;
            }),
            // stop producer before it is scheduled to run
            mi::on_starting([&, at](auto up) {
              signals_ += 10;
              mi::set_value(up, 1);
              tcncl_ |
                op::schedule_at(at) |
                op::submit([up = std::move(up)](auto) mutable {
                ::mi::set_done(up);
              });
            })));

    while (terminal_ == 0 || cancel_ == 0) {
      std::this_thread::yield();
    }
  }

  NT ntproduce_{mi::new_thread()};
  mi::time_source<> timeproduce_{};
  TNT tnt_{make_time(timeproduce_, ntproduce_)};

  NT ntcancel_{mi::new_thread()};
  mi::time_source<> timecancel_{};
  TNT tcncl_{make_time(timecancel_, ntcancel_)};

  std::atomic<int> signals_{0};
  std::atomic<int> terminal_{0};
  std::atomic<int> cancel_{0};
  std::chrono::system_clock::time_point at_{mi::now(tnt_) + 100ms};
};

TEST_F(ConcurrentFlowManySender, EarlyCancellation) {
  // this nightmare brought to you by ASAN stack-use-after-return.
  cancellation_test(at_ - 50ms);

  join();

  EXPECT_THAT(signals_, Eq(1010011))
      << "expected that the starting, up.done and out.done signals are each recorded once";
}

TEST_F(ConcurrentFlowManySender, LateCancellation) {
  // this nightmare brought to you by ASAN stack-use-after-return.
  cancellation_test(at_ + 50ms);

  join();

  EXPECT_THAT(signals_, Eq(1010111))
      << "expected that the starting, up.done and out.value signals are each recorded once";
}

TEST_F(ConcurrentFlowManySender, RacingCancellation) {
  int total = 0;
  int cancellostrace = 0; // 1010111
  int cancelled = 0; // 1010011

  for (;;) {
    reset();
    cancellation_test(at_);

    // accumulate known signals
    ++total;
    cancellostrace += signals_ == 1010111;
    cancelled += signals_ == 1010011;

    EXPECT_THAT(total, Eq(cancellostrace + cancelled))
        << signals_ << " <- this set of signals is unrecognized";

    ASSERT_THAT(total, Lt(100))
        // too long, abort and show the signals distribution
        << "total " << total << ", cancel-lost-race " << cancellostrace
        << ", cancelled " << cancelled;

    if (cancellostrace > 4 && cancelled > 4) {
      // yay all known outcomes were observed!
      break;
    }
    // try again
    continue;
  }

  join();
}

TEST(FlowManySender, From) {
  auto v = std::array<int, 5>{{0, 1, 2, 3, 4}};
  auto f = op::flow_from(v);

  int actual = 0;
  f | op::for_each(mi::MAKE(receiver)([&](int) { ++actual; }));

  EXPECT_THAT(actual, Eq(5)) << "expexcted that all the values are sent once";
}
