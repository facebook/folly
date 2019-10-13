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
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/schedule.h>

#include <folly/experimental/pushmi/entangle.h>
#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/time_source.h>
#include <folly/experimental/pushmi/executor/trampoline.h>
#include <folly/functional/Invoke.h>

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

namespace {
struct set_stop_type {
  template <typename Stop>
  void operator()(Stop& stop) const {
    if (!!stop) {
      *stop = true;
    }
  }
};
} // namespace

class ImmediateFlowSingleSender : public Test {
 protected:
  auto make_producer() {
    return mi::MAKE(flow_single_sender)([&](auto out) {
      // boolean cancellation
      bool stop = false;
      auto set_stop = set_stop_type{};
      auto tokens = mi::shared_entangle(stop, set_stop);

      using Stopper = decltype(tokens.second);
      struct Data : mi::receiver<> {
        explicit Data(Stopper stopper_) : stopper(std::move(stopper_)) {}
        Stopper stopper;
      };
      auto up = mi::MAKE(receiver)(
          Data{std::move(tokens.second)},
          [&](auto& data, auto) noexcept {
            signals_ += 100000;
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          },
          mi::on_done([&](auto& data) {
            signals_ += 10000;
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          }));

      // pass reference for cancellation.
      ::mi::set_starting(out, std::move(up));

      // check boolean to select signal
      auto both = lock_both(tokens.first);
      if (!!both.first && !*(both.first)) {
        ::mi::set_value(out, 42);
      } else {
        // cancellation is not an error
        ::mi::set_done(out);
      }
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

TEST_F(ImmediateFlowSingleSender, EarlyCancellation) {
  make_producer() | op::submit(make_consumer([](auto up) {
    // immediately stop producer
    ::mi::set_done(up);
  }));

  EXPECT_THAT(signals_, Eq(10011))
      << "expected that the starting, up.done and out.done signals are each recorded once";
}

TEST_F(ImmediateFlowSingleSender, LateCancellation) {
  make_producer() | op::submit(make_consumer([](auto) {
    // do not stop producer before it is scheduled to run
  }));

  EXPECT_THAT(signals_, Eq(110))
      << "expected that the starting and out.value signals are each recorded once";
}

using NT = decltype(mi::new_thread());

inline auto make_time(mi::time_source<>& t, NT& ex) {
  auto strands = t.make(mi::systemNowF{}, ex);
  return mi::make_strand(strands);
}

class ConcurrentFlowSingleSender : public Test {
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

  template <class MakeTokens>
  void cancellation_test(
      std::chrono::system_clock::time_point at,
      MakeTokens make_tokens) {
    using tokens_type =
        folly::invoke_result_t<MakeTokens&, bool&, set_stop_type&>;
    auto f = mi::MAKE(flow_single_sender)([&, make_tokens](auto out) {
      // boolean cancellation
      bool stop = false;
      auto set_stop = set_stop_type{};
      tokens_type tokens = make_tokens(stop, set_stop);

      using Stopper = decltype(tokens.second);
      struct Data : mi::receiver<> {
        explicit Data(Stopper stopper_) : stopper(std::move(stopper_)) {}
        Stopper stopper;
      };
      auto up = mi::MAKE(receiver)(
          Data{std::move(tokens.second)},
          [&](auto& data, auto) noexcept {
            signals_ += 100000;
            ++cancel_;
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          },
          mi::on_done([&](auto& data) {
            signals_ += 10000;
            ++cancel_;
            auto both = lock_both(data.stopper);
            (*(both.first))(both.second);
          }));

      // make all the signals come from the same thread
      tnt_ |
          op::schedule() |
          op::submit([&,
                      stoppee = std::move(tokens.first),
                      up_not_a_shadow_howtoeven = std::move(up),
                      out = std::move(out)](auto tnt) mutable {
            // pass reference for cancellation.
            ::mi::set_starting(out, std::move(up_not_a_shadow_howtoeven));

            // submit work to happen later
            tnt |
              op::schedule_at(at_) |
                op::submit(
                    [stoppee = std::move(stoppee),
                     out = std::move(out)](auto) mutable {
                      // check boolean to select signal
                      auto both = lock_both(stoppee);
                      if (!!both.first && !*(both.first)) {
                        ::mi::set_value(out, 42);
                        ::mi::set_done(out);
                      } else {
                        // cancellation is not an error
                        ::mi::set_done(out);
                      }
                    });
          });
    });

    f |
        op::submit(
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
              tcncl_ | op::schedule_at(at) | op::submit([up = std::move(up)](auto) mutable {
                ::mi::set_done(up);
              });
            }));

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

TEST_F(ConcurrentFlowSingleSender, EarlySharedCancellation) {
  cancellation_test(at_ - 50ms, [](auto& stop, auto& set_stop) {
    return mi::shared_entangle(stop, set_stop);
  });

  join();

  EXPECT_THAT(signals_, Eq(10011));
}

TEST_F(ConcurrentFlowSingleSender, LateSharedCancellation) {
  cancellation_test(at_ + 50ms, [](auto& stop, auto& set_stop) {
    return mi::shared_entangle(stop, set_stop);
  });

  join();

  EXPECT_THAT(signals_, Eq(10111))
      << "expected that the starting, up.done, out.value and out.done signals are each recorded once";
}

TEST_F(ConcurrentFlowSingleSender, RacingSharedCancellation) {
  int total = 0;
  int cancellostrace = 0; // 10111
  int cancelled = 0; // 10011

  for (;;) {
    reset();
    cancellation_test(at_, [](auto& stop, auto& set_stop) {
      return mi::shared_entangle(stop, set_stop);
    });

    // accumulate known signals
    ++total;
    cancellostrace += signals_ == 10111;
    cancelled += signals_ == 10011;

    EXPECT_THAT(total, Eq(cancellostrace + cancelled))
        << signals_ << " <- this set of signals is unrecognized";

    ASSERT_THAT(total, Lt(100))
        // too long, show the signals distribution
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

TEST_F(ConcurrentFlowSingleSender, EarlyEntangledCancellation) {
  cancellation_test(at_ - 50ms, [](auto& stop, auto& set_stop) {
    return mi::entangle(stop, set_stop);
  });

  join();

  EXPECT_THAT(signals_, Eq(10011));
}

TEST_F(ConcurrentFlowSingleSender, LateEntangledCancellation) {
  cancellation_test(at_ + 50ms, [](auto& stop, auto& set_stop) {
    return mi::entangle(stop, set_stop);
  });

  join();

  EXPECT_THAT(signals_, Eq(10111))
      << "expected that the starting, up.done, out.value and out.done signals are each recorded once";
}

TEST_F(ConcurrentFlowSingleSender, RacingEntangledCancellation) {
  int total = 0;
  int cancellostrace = 0; // 10111
  int cancelled = 0; // 10011

  for (;;) {
    reset();
    cancellation_test(at_, [](auto& stop, auto& set_stop) {
      return mi::entangle(stop, set_stop);
    });

    // accumulate known signals
    ++total;
    cancellostrace += signals_ == 10111;
    cancelled += signals_ == 10011;

    EXPECT_THAT(total, Eq(cancellostrace + cancelled))
        << signals_ << " <- this set of signals is unrecognized";

    ASSERT_THAT(total, Lt(100))
        // too long, show the signals distribution
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
