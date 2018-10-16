#include "catch.hpp"

#include <array>

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include "pushmi/flow_many_sender.h"
#include "pushmi/o/submit.h"
#include "pushmi/o/from.h"
#include "pushmi/o/for_each.h"

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

SCENARIO("flow many immediate cancellation", "[flow][sender]") {
  int signals = 0;

  GIVEN("A flow many sender") {
    auto f = mi::MAKE(flow_many_sender)([&](auto out) {

      using Out = decltype(out);
      struct Data : mi::many<> {
        explicit Data(Out out) : out(std::move(out)), stop(false) {}
        Out out;
        bool stop;
      };

      auto up = mi::MAKE(many)(
          Data{std::move(out)},
          [&](auto& data, auto requested) {
            signals += 1000000;
            if (requested < 1) {return;}
            // check boolean to select signal
            if (!data.stop) {
              ::mi::set_next(data.out, 42);
            }
            ::mi::set_done(data.out);
          },
          [&](auto& data, auto e) noexcept {
            signals += 100000;
            data.stop = true;
            ::mi::set_done(data.out);
          },
          [&](auto& data) {
            signals += 10000;
            data.stop = true;
            ::mi::set_done(data.out);
          });

      // pass reference for cancellation.
      ::mi::set_starting(up.data().out, std::move(up));
    });

    WHEN("submit is applied and cancels the producer") {
      f |
          op::submit(mi::MAKE(flow_many)(
              mi::on_next([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // immediately stop producer
              mi::on_starting([&](auto up) {
                signals += 10;
                ::mi::set_done(up);
              })));

      THEN(
          "the starting, up.done and out.done signals are each recorded once") {
        REQUIRE(signals == 10011);
      }
    }

    WHEN("submit is applied and cancels the producer late") {
      f |
          op::submit(mi::MAKE(flow_many)(
              mi::on_next([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                ::mi::set_next(up, 1);
              })));

      THEN(
          "the starting, up.next, next and done signals are each recorded once") {
        REQUIRE(signals == 1000111);
      }
    }

  }
}

SCENARIO("flow many cancellation trampoline", "[flow][sender]") {
  auto tr = mi::trampoline();
  using TR = decltype(tr);
  int signals = 0;

  GIVEN("A flow many sender") {
    auto f = mi::MAKE(flow_many_sender)([&](auto out) {
      using Out = decltype(out);
      struct Data : mi::many<> {
        explicit Data(Out out) : out(std::move(out)), stop(false), valid(true) {}
        Out out;
        bool stop;
        bool valid;
      };

      auto up = mi::MAKE(many)(
          Data{std::move(out)},
          [&](auto& data, auto requested) {
            signals += 1000000;
            if (requested < 1 || data.stop) {return;}
            // submit work to happen later
            auto datal = std::move(data);
            data.valid = false;
            tr |
                op::submit_after(
                    100ms,
                    [data = std::move(datal)](auto) mutable {
                      // check boolean to select signal
                      if (!data.stop) {
                        ::mi::set_next(data.out, 42);
                      }
                      ::mi::set_done(data.out);
                    });
          },
          [&](auto& data, auto e) noexcept {
            signals += 100000;
            if(data.stop || !data.valid) {return;}
            data.stop = true;
            ::mi::set_done(data.out);
          },
          [&](auto& data) {
            signals += 10000;
            if(data.stop || !data.valid) {return;}
            data.stop = true;
            ::mi::set_done(data.out);
          });

      tr |
          op::submit([up = std::move(up)](auto tr) mutable {
            // pass reference for cancellation.
            ::mi::set_starting(up.data().out, std::move(up));
          });
    });

    WHEN("submit is applied and cancels the producer") {
      f |
          op::submit(
              mi::on_next([&](int) { signals += 100; }),
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
              mi::on_next([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                mi::set_next(up, 1);
                tr |
                    op::submit_after(250ms, [up = std::move(up)](auto) mutable {
                      ::mi::set_done(up);
                    });
              }));

      THEN(
          "the starting, up.done and out.value signals are each recorded once") {
        REQUIRE(signals == 1010111);
      }
    }
  }
}

SCENARIO("flow many cancellation new thread", "[flow][sender]") {
  auto nt = mi::new_thread();
  using NT = decltype(nt);
  std::atomic<int> signals{0};
  auto at = nt.now() + 200ms;

  GIVEN("A flow many sender") {
    auto f = mi::MAKE(flow_many_sender)([&](auto out) {
      using Out = decltype(out);

      // boolean cancellation
      struct producer {
        producer(Out out, NT nt, bool s) : out(std::move(out)), nt(std::move(nt)), stop(s) {}
        Out out;
        NT nt;
        std::atomic<bool> stop;
      };
      auto p = std::make_shared<producer>(std::move(out), nt, false);

      struct Data : mi::many<> {
        explicit Data(std::shared_ptr<producer> p) : p(std::move(p)) {}
        std::shared_ptr<producer> p;
      };

      auto up = mi::MAKE(many)(
          Data{p},
          [&at, &signals](auto& data, auto requested) {
            signals += 1000000;
            if (requested < 1) {return;}
            // submit work to happen later
            data.p->nt |
                op::submit_at(
                    at,
                    [p = data.p](auto)  {
                      // check boolean to select signal
                      if (!p->stop) {
                        ::mi::set_next(p->out, 42);
                      }
                      ::mi::set_done(p->out);
                    });
          },
          [&signals](auto& data, auto e) noexcept {
            signals += 100000;
            data.p->stop.store(true);
            data.p->nt | op::submit([p = data.p](auto)  {
              ::mi::set_done(p->out);
            });
          },
          [&signals](auto& data) {
            signals += 10000;
            data.p->stop.store(true);
            data.p->nt | op::submit([p = data.p](auto)  {
              ::mi::set_done(p->out);
            });
          });

      nt |
          op::submit([p, up = std::move(up)](auto nt) mutable {
            // pass reference for cancellation.
            ::mi::set_starting(p->out, std::move(up));
          });
    });

    WHEN("submit is applied and cancels the producer early") {
      {
      f |
          op::blocking_submit(
              mi::on_next([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                mi::set_next(up, 1);
                nt |
                    op::submit_at(
                        at - 100ms, [up = std::move(up)](auto) mutable {
                          ::mi::set_done(up);
                        });
              }));
      }

      // make sure that the completion signal arrives
      std::this_thread::sleep_for(200ms);

      THEN(
          "the starting, up.done and out.done signals are each recorded once") {
        REQUIRE(signals == 1010011);
      }
    }

    WHEN("submit is applied and cancels the producer late") {
      {
      f |
          op::blocking_submit(
              mi::on_next([&](int) { signals += 100; }),
              mi::on_error([&](auto) noexcept { signals += 1000; }),
              mi::on_done([&]() { signals += 1; }),
              // do not stop producer before it is scheduled to run
              mi::on_starting([&](auto up) {
                signals += 10;
                mi::set_next(up, 1);
                nt |
                    op::submit_at(
                        at + 100ms, [up = std::move(up)](auto) mutable {
                          ::mi::set_done(up);
                        });
              }));
      }

      std::this_thread::sleep_for(200ms);

      THEN(
          "the starting, up.done and out.value signals are each recorded once") {
        REQUIRE(signals == 1010111);
      }
    }

    WHEN("submit is applied and cancels the producer at the same time") {
      // count known results
      int total = 0;
      int cancellostrace = 0; // 1010111
      int cancelled = 0; // 1010011

      for (;;) {
        signals = 0;
        // set completion time to be in 100ms
        at = nt.now() + 100ms;
        {
        f |
            op::blocking_submit(
                mi::on_next([&](int) { signals += 100; }),
                mi::on_error([&](auto) noexcept { signals += 1000; }),
                mi::on_done([&]() { signals += 1; }),
                // stop producer at the same time that it is scheduled to run
                mi::on_starting([&](auto up) {
                  signals += 10;
                  mi::set_next(up, 1);
                  nt | op::submit_at(at, [up = std::move(up)](auto) mutable {
                    ::mi::set_done(up);
                  });
                }));
        }

        // make sure any cancellation signal has completed
        std::this_thread::sleep_for(10ms);

        // accumulate known signals
        ++total;
        cancellostrace += signals == 1010111;
        cancelled += signals == 1010011;

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

SCENARIO("flow many from", "[flow][sender][for_each]") {
  GIVEN("A flow many sender of 5 values") {
    auto v = std::array<int, 5>{0, 1, 2, 3, 4};
    auto f = op::flow_from(v);

    WHEN("for_each is applied") {
      int actual = 0;
      f | op::for_each(mi::MAKE(many)([&](int){++actual;}));

      THEN("all the values are sent once") {
        REQUIRE(actual == 5);
      }
    }
  }
}
