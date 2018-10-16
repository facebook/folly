#include "catch.hpp"

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include "pushmi/flow_single_deferred.h"
#include "pushmi/o/submit.h"

#include "pushmi/trampoline.h"
#include "pushmi/new_thread.h"

using namespace pushmi::aliases;

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) {__VA_ARGS__}
#else
#define MAKE(x) make_ ## x
#endif

SCENARIO( "flow single immediate cancellation", "[flow][deferred]" ) {

  int signals = 0;

  GIVEN( "A flow single deferred" ) {

    auto f = mi::MAKE(flow_single_deferred)([&](auto out){

      // boolean cancellation - on stack
      bool stop = false;
      auto up = mi::MAKE(none)(
        [&](auto e) noexcept {signals += 1000000; stop = true;},
        [&](){signals += 100000; stop = true;});

      // pass reference for cancellation.
      ::mi::set_starting(out, up);

      // check boolean to select signal
      if (!stop) {
        ::mi::set_value(out, 42);
      } else {
        // cancellation is not an error
        ::mi::set_done(out);
      }
      // I want to get rid of this signal it makes usage harder and
      // messes up r-value qualifing done, error and value.
      ::mi::set_stopping(out);
    });

    WHEN( "submit is applied and cancels the producer" ) {

      f | op::submit(
          mi::on_value([&](int){ signals = 100; }),
          mi::on_error([&](auto) noexcept { signals = 1000; }),
          mi::on_done([&](){signals += 1;}),
          mi::on_stopping([&](){signals += 10000;}),
          // immediately stop producer
          mi::on_starting([&](auto up){ signals = 10; ::mi::set_done(up); }));

      THEN( "the starting, up.done, out.done out.stopping signals are each recorded once" ) {
        REQUIRE(signals == 110011);
      }
    }
  }
}

SCENARIO( "flow single cancellation", "[flow][deferred]" ) {

  auto tr = v::trampoline();
  using TR = decltype(tr);
  int signals = 0;

  GIVEN( "A flow single deferred" ) {

    auto f = mi::MAKE(flow_single_deferred)([&](auto out){

      // boolean cancellation - on stack
      bool stop = false;
      auto up = mi::MAKE(none)(
        [&](auto e) noexcept {signals += 1000000; stop = true;},
        [&](){signals += 100000; stop = true;});

      // blocking the stack to keep 'up' alive
      tr | op::blocking_submit([&](auto tr){

        // pass reference for cancellation.
        ::mi::set_starting(out, up);

        tr | op::submit_after(
          100ms,
          [&](auto) {
            // check boolean to select signal
            if (!stop) {
              ::mi::set_value(out, 42);
            } else {
              // cancellation is not an error
              ::mi::set_done(out);
            }
            // I want to get rid of this signal it makes usage harder and
            // messes up r-value qualifing done, error and value.
            ::mi::set_stopping(out);
          }
        );
      });
    });

    WHEN( "submit is applied and cancels the producer" ) {

      f | op::submit(
          mi::on_value([&](int){ signals = 100; }),
          mi::on_error([&](auto) noexcept { signals = 1000; }),
          mi::on_done([&](){signals += 1;}),
          mi::on_stopping([&](){signals += 10000;}),
          // stop producer before it is scheduled to run
          mi::on_starting([&](auto up){ signals = 10; tr | op::submit_after(50ms, [up](auto) mutable {::mi::set_done(up);}); }));

      THEN( "the starting, up.done, out.done out.stopping signals are each recorded once" ) {
        REQUIRE(signals == 110011);
      }
    }
  }
}
