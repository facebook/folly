#include "catch.hpp"

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include "pushmi/flow_single_deferred.h"
#include "pushmi/o/submit.h"

#include "pushmi/entangle.h"
#include "pushmi/new_thread.h"
#include "pushmi/trampoline.h"

using namespace pushmi::aliases;

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) \
  { __VA_ARGS__ }
#else
#define MAKE(x) make_##x
#endif

SCENARIO("flow single immediate cancellation", "[flow][deferred]") {
  int signals = 0;

  GIVEN("A flow single deferred") {
    auto f = mi::MAKE(flow_single_deferred)([&](auto out) {
      // boolean cancellation
      bool stop = false;
      auto set_stop = [](auto& e) {
        auto stop = e.lockPointerToDual();
        if (!!stop) {
          *stop = true;
        }
        e.unlockPointerToDual();
      };
      auto tokens = mi::entangle(stop, set_stop);

      using Stopper = decltype(tokens.second);
      struct Data : mi::none<> {
        explicit Data(Stopper stopper) : stopper(std::move(stopper)) {}
        Stopper stopper;
      };
      auto up = mi::MAKE(none)(
          Data{std::move(tokens.second)},
          [&](auto& data, auto e) noexcept {
            signals += 100000;
            data.stopper.t(data.stopper);
          },
          [&](auto& data) {
            signals += 10000;
            data.stopper.t(data.stopper);
          });

      // pass reference for cancellation.
      ::mi::set_starting(out, std::move(up));

      // check boolean to select signal
      if (!tokens.first.t) {
        ::mi::set_value(out, 42);
      } else {
        // cancellation is not an error
        ::mi::set_done(out);
      }
    });

    WHEN("submit is applied and cancels the producer") {
      f |
          op::submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // immediately stop producer
              mi::on_starting([&](auto up) {
                signals += 10;
                ::mi::set_done(up);
              }));

      THEN(
          "the starting, up.done and out.done signals are each recorded once") {
        REQUIRE(signals == 10011);
      }
    }

    WHEN("submit is applied and cancels the producer late") {
      f |
          op::submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) { signals += 10; }));

      THEN(
          "the starting and out.value signals are each recorded once") {
        REQUIRE(signals == 110);
      }
    }
  }
}

SCENARIO("flow single cancellation trampoline", "[flow][deferred]") {
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  int signals = 0;

  GIVEN("A flow single deferred") {
    auto f = mi::MAKE(flow_single_deferred)([&](auto out) {
      // boolean cancellation
      bool stop = false;
      auto set_stop = [](auto& e) {
        auto stop = e.lockPointerToDual();
        if (!!stop) {
          *stop = true;
        }
        e.unlockPointerToDual();
      };
      auto tokens = mi::entangle(stop, set_stop);

      using Stopper = decltype(tokens.second);
      struct Data : mi::none<> {
        explicit Data(Stopper stopper) : stopper(std::move(stopper)) {}
        Stopper stopper;
      };
      auto up = mi::MAKE(none)(
          Data{std::move(tokens.second)},
          [&](auto& data, auto e) noexcept {
            signals += 100000;
            data.stopper.t(data.stopper);
          },
          [&](auto& data) {
            signals += 10000;
            data.stopper.t(data.stopper);
          });

      tr |
          op::submit([out = std::move(out),
                      up = std::move(up),
                      stoppee = std::move(tokens.first)](auto tr) mutable {
            // pass reference for cancellation.
            ::mi::set_starting(out, std::move(up));

            // submit work to happen later
            tr |
                op::submit_after(
                    100ms,
                    [out = std::move(out),
                     stoppee = std::move(stoppee)](auto) mutable {
                      // check boolean to select signal
                      if (!stoppee.t) {
                        ::mi::set_value(out, 42);
                      } else {
                        // cancellation is not an error
                        ::mi::set_done(out);
                      }
                    });
          });
    });

    WHEN("submit is applied and cancels the producer") {
      f |
          op::submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                tr | op::submit_after(50ms, [up = std::move(up)](auto) mutable {
                  ::mi::set_done(up);
                });
              }));

      THEN(
          "the starting, up.done and out.done signals are each recorded once") {
        REQUIRE(signals == 10011);
      }
    }

    WHEN("submit is applied and cancels the producer late") {
      f |
          op::submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                tr |
                    op::submit_after(250ms, [up = std::move(up)](auto) mutable {
                      ::mi::set_done(up);
                    });
              }));

      THEN(
          "the starting, up.done and out.value signals are each recorded once") {
        REQUIRE(signals == 10110);
      }
    }
  }
}

// copies the value of the atomic during move. requires external Synchronization
// to make the copy safe. entangle() provides the needed external
// Synchronization
template <class T>
struct moving_atomic : std::atomic<T> {
  using std::atomic<T>::atomic;
  moving_atomic(moving_atomic&& o) : std::atomic<T>(o.load()) {}
};

SCENARIO("flow single cancellation new thread", "[flow][deferred]") {
  auto nt = mi::new_thread();
  using NT = decltype(nt);
  std::atomic<int> signals{0};
  auto at = nt.now() + 200ms;

  GIVEN("A flow single deferred") {
    auto f = mi::MAKE(flow_single_deferred)([&](auto out) {
      // boolean cancellation
      moving_atomic<bool> stop = false;
      auto set_stop = [](auto& e) {
        auto stop = e.lockPointerToDual();
        if (!!stop) {
          stop->store(true);
        }
        e.unlockPointerToDual();
      };
      auto tokens = mi::entangle(std::move(stop), std::move(set_stop));

      using Stopper = decltype(tokens.second);
      struct Data : mi::none<> {
        explicit Data(Stopper stopper) : stopper(std::move(stopper)) {}
        Stopper stopper;
      };
      auto up = mi::MAKE(none)(
          Data{std::move(tokens.second)},
          [&](auto& data, auto e) noexcept {
            signals += 100000;
            data.stopper.t(data.stopper);
          },
          [&](auto& data) {
            signals += 10000;
            data.stopper.t(data.stopper);
          });

      // make all the signals come from the same thread
      nt |
          op::submit([stoppee = std::move(tokens.first),
                      up = std::move(up),
                      out = std::move(out),
                      at](auto nt) mutable {
            // pass reference for cancellation.
            ::mi::set_starting(out, std::move(up));

            // submit work to happen later
            nt |
                op::submit_at(
                    at,
                    [stoppee = std::move(stoppee),
                     out = std::move(out)](auto) mutable {
                      // check boolean to select signal
                      if (!stoppee.t.load()) {
                        ::mi::set_value(out, 42);
                      } else {
                        // cancellation is not an error
                        ::mi::set_done(out);
                      }
                    });
          });
    });

    WHEN("submit is applied and cancels the producer early") {
      {
      f |
          op::blocking_submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                nt |
                    op::submit_at(
                        at - 50ms, [up = std::move(up)](auto) mutable {
                          ::mi::set_done(up);
                        });
              }));
      }

      // make sure that the completion signal arrives
      std::this_thread::sleep_for(100ms);

      THEN(
          "the starting, up.done and out.done signals are each recorded once") {
        REQUIRE(signals == 10011);
      }
    }

    WHEN("submit is applied and cancels the producer late") {
      {
      f |
          op::blocking_submit(
              mi::on_value([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                nt |
                    op::submit_at(
                        at + 50ms, [up = std::move(up)](auto) mutable {
                          ::mi::set_done(up);
                        });
              }));
      }

      std::this_thread::sleep_for(100ms);

      THEN(
          "the starting, up.done and out.value signals are each recorded once") {
        REQUIRE(signals == 10110);
      }
    }

    WHEN("submit is applied and cancels the producer at the same time") {
      // count known results
      int total = 0;
      int cancellostrace = 0; // 10110
      int cancelled = 0; // 10011

      for (;;) {
        signals = 0;
        // set completion time to be in 100ms
        at = nt.now() + 100ms;
        {
        f |
            op::blocking_submit(
                mi::on_value([&](int) { signals += 100; }),
                mi::on_error([&](auto) noexcept { signals += 1000; }),
                mi::on_done([&]() { signals += 1; }),
                // stop producer at the same time that it is scheduled to run
                mi::on_starting([&](auto up) {
                  signals += 10;
                  nt | op::submit_at(at, [up = std::move(up)](auto) mutable {
                    ::mi::set_done(up);
                  });
                }));
        }

        // make sure any cancellation signal has completed
        std::this_thread::sleep_for(10ms);

        // accumulate known signals
        ++total;
        cancellostrace += signals == 10110;
        cancelled += signals == 10011;

        if (total != cancellostrace + cancelled) {
          // display the unrecognized signals recorded
          REQUIRE(signals == -1);
        }
        if (total >= 100) {
          // too long, abort and show the signals distribution
          WARN(
              "total " << total << ", cancel-lost-race " << cancellostrace
                       << ", cancelled " << cancelled);
          break;
        }
        if (cancellostrace > 4 && cancelled > 4) {
          // yay all known outcomes were observed!
          break;
        }
        // try again
        continue;
      }
    }
  }
}
