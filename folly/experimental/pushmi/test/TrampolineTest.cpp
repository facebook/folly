#include "catch.hpp"

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include "pushmi/flow_single_sender.h"
#include "pushmi/o/empty.h"
#include "pushmi/o/just.h"
#include "pushmi/o/on.h"
#include "pushmi/o/transform.h"
#include "pushmi/o/tap.h"
#include "pushmi/o/via.h"
#include "pushmi/o/submit.h"
#include "pushmi/o/extension_operators.h"

#include "pushmi/inline.h"
#include "pushmi/trampoline.h"

using namespace pushmi::aliases;

struct countdownsingle {
  countdownsingle(int& c)
      : counter(&c) {}

  int* counter;

  template <class ExecutorRef>
  void operator()(ExecutorRef exec) {
    if (--*counter > 0) {
      exec | op::submit(*this);
    }
  }
};

SCENARIO( "trampoline executor", "[trampoline][sender]" ) {

  GIVEN( "A trampoline time_single_sender" ) {
    auto tr = v::trampoline();
    using TR = decltype(tr);

    // REQUIRE( v::TimeSingleDeferred<
    //   TR, v::archetype_single,
    //   TR&, std::exception_ptr> );
    // REQUIRE( v::TimeExecutor<
    //   TR&, v::archetype_single,
    //   std::exception_ptr> );

    WHEN( "submit now" ) {
      auto signals = 0;
      auto start = v::now(tr);
      auto signaled = v::now(tr);
      tr |
        op::transform([](auto tr){ return v::now(tr); }) |
        op::submit(
          [&](auto at){ signaled = at;
            signals += 100; },
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; });

      THEN( "the value signal is recorded once and the signal did not drift much" ) {
        REQUIRE( signals == 100 );
        INFO("The delay is " << ::Catch::Detail::stringify(signaled - start));
        REQUIRE( signaled - start < 10s );
      }
    }

    WHEN( "blocking get now" ) {
      auto start = v::now(tr);
      auto signaled = tr |
        op::transform([](auto tr){ return v::now(tr); }) |
        op::get<decltype(v::now(tr))>;

      THEN( "the signal did not drift much" ) {
        INFO("The delay is " << ::Catch::Detail::stringify(signaled - start));
        REQUIRE( signaled - start < 10s );
      }
    }

    WHEN( "submissions are ordered in time" ) {
      std::vector<std::string> times;
      auto push = [&](int time) {
        return v::on_value([&, time](auto) { times.push_back(std::to_string(time)); });
      };
      tr | op::submit(v::on_value([push](auto tr) {
        tr |
            op::submit_after(40ms, push(40)) |
            op::submit_after(10ms, push(10)) |
            op::submit_after(20ms, push(20)) |
            op::submit_after(10ms, push(11));
      }));

      THEN( "the items were pushed in time order not insertion order" ) {
        REQUIRE( times == std::vector<std::string>{"10", "11", "20", "40"});
      }
    }

    WHEN( "now is called" ) {
      bool done = false;
      tr | ep::now();
      tr | op::submit([&](auto tr) {
        tr | ep::now();
        done = true;
      });

      THEN( "both calls to now() complete" ) {
        REQUIRE( done == true );
      }
    }

    WHEN( "virtual derecursion is triggered" ) {
      int counter = 100'000;
      std::function<void(pushmi::any_time_executor_ref<> exec)> recurse;
      recurse = [&](pushmi::any_time_executor_ref<> tr) {
        if (--counter <= 0)
          return;
        tr | op::submit(recurse);
      };
      tr | op::submit([&](auto exec) { recurse(exec); });

      THEN( "all nested submissions complete" ) {
        REQUIRE( counter == 0 );
      }
    }

    WHEN( "static derecursion is triggered" ) {
      int counter = 100'000;
      countdownsingle single{counter};
      tr | op::submit(single);
      THEN( "all nested submissions complete" ) {
        REQUIRE( counter == 0 );
      }
    }

    WHEN( "used with on" ) {
      std::vector<std::string> values;
      auto sender = pushmi::make_single_sender([](auto out) {
        ::pushmi::set_value(out, 2.0);
        // ignored
        ::pushmi::set_value(out, 1);
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
      });
      auto inlineon = sender | op::on([&](){return mi::inline_executor();});
      inlineon |
          op::submit(v::on_value([&](auto v) { values.push_back(std::to_string(v)); }));
      THEN( "only the first item was pushed" ) {
        REQUIRE(values == std::vector<std::string>{"2.000000"});
      }
      THEN( "executor was not changed by on" ) {
        REQUIRE(std::is_same<mi::executor_t<decltype(sender)>, mi::executor_t<decltype(inlineon)>>::value);
      }
    }

    WHEN( "used with via" ) {
      std::vector<std::string> values;
      auto sender = pushmi::make_single_sender([](auto out) {
        ::pushmi::set_value(out, 2.0);
        // ignored
        ::pushmi::set_value(out, 1);
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
      });
      auto inlinevia = sender | op::via([&](){return mi::inline_executor();});
      inlinevia |
          op::submit(v::on_value([&](auto v) { values.push_back(std::to_string(v)); }));
      THEN( "only the first item was pushed" ) {
        REQUIRE(values == std::vector<std::string>{"2.000000"});
      }
      THEN( "executor was changed by via" ) {
        REQUIRE(!std::is_same<mi::executor_t<decltype(sender)>, mi::executor_t<decltype(inlinevia)>>::value);
      }
    }
  }
}
