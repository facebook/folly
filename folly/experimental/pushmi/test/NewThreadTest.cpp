#include "catch.hpp"

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include "pushmi/flow_single_deferred.h"
#include "pushmi/o/empty.h"
#include "pushmi/o/just.h"
#include "pushmi/o/on.h"
#include "pushmi/o/transform.h"
#include "pushmi/o/tap.h"
#include "pushmi/o/via.h"
#include "pushmi/o/submit.h"
#include "pushmi/o/extension_operators.h"

#include "pushmi/trampoline.h"
#include "pushmi/new_thread.h"

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

SCENARIO( "new_thread executor", "[new_thread][deferred]" ) {

  GIVEN( "A new_thread time_single_deferred" ) {
    auto nt = v::new_thread();
    using NT = decltype(nt);

    // REQUIRE( v::TimeSingleDeferred<
    //   NT, v::archetype_single,
    //   NT&, std::exception_ptr> );
    // REQUIRE( v::TimeExecutor<
    //   NT&, v::archetype_single,
    //   std::exception_ptr> );

    auto any = v::any_time_executor{nt};

    WHEN( "blocking submit now" ) {
      auto signals = 0;
      auto start = v::now(nt);
      auto signaled = v::now(nt);
      nt |
        op::transform([](auto nt){ return nt | ep::now(); }) |
        op::blocking_submit(
          [&](auto at){
            signaled = at;
            signals += 100; },
          [&](auto e) noexcept {  signals += 1000; },
          [&](){ signals += 10; });

      THEN( "the value signal is recorded once and the signal did not drift much" ) {
        REQUIRE( signals == 100 );
        INFO("The delay is " << ::Catch::Detail::stringify(signaled - start));
        REQUIRE( signaled - start < 10s );
      }
    }

    WHEN( "blocking get now" ) {
      auto start = v::now(nt);
      auto signaled = nt |
        op::transform([](auto nt){
          return v::now(nt);
        }) |
        op::get<std::chrono::system_clock::time_point>;

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
      nt | op::blocking_submit(v::on_value{[push](auto nt) {
        nt |
            op::submit_after(40ms, push(40)) |
            op::submit_after(10ms, push(10)) |
            op::submit_after(20ms, push(20)) |
            op::submit_after(10ms, push(11));
      }});

      THEN( "the items were pushed in time order not insertion order" ) {
        REQUIRE( times == std::vector<std::string>{"10", "11", "20", "40"});
      }
    }

    WHEN( "now is called" ) {
      bool done = false;
      nt | ep::now();
      nt | op::blocking_submit([&](auto nt) {
        nt | ep::now();
        done = true;
      });

      THEN( "both calls to now() complete" ) {
        REQUIRE( done == true );
      }
    }

    WHEN( "virtual derecursion is triggered" ) {
      int counter = 100'000;
      std::function<void(pushmi::archtype_any_time_executor_ref exec)> recurse;
      recurse = [&](pushmi::archtype_any_time_executor_ref nt) {
        if (--counter <= 0)
          return;
        nt | op::submit(recurse);
      };
      nt | op::blocking_submit([&](auto nt) { recurse(nt); });

      THEN( "all nested submissions complete" ) {
        REQUIRE( counter == 0 );
      }
    }

    WHEN( "static derecursion is triggered" ) {
      int counter = 100'000;
      countdownsingle single{counter};
      nt | op::blocking_submit(single);
      THEN( "all nested submissions complete" ) {
        REQUIRE( counter == 0 );
      }
    }

    WHEN( "used with on" ) {
      std::vector<std::string> values;
      auto deferred = pushmi::single_deferred([](auto out) {
        ::pushmi::set_value(out, 2.0);
        // ignored
        ::pushmi::set_value(out, 1);
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
      });
      deferred | op::on([&](){return nt;}) |
          op::blocking_submit(v::on_value{[&](auto v) { values.push_back(std::to_string(v)); }});
      THEN( "only the first item was pushed" ) {
        REQUIRE(values == std::vector<std::string>{"2.000000"});
      }
    }

    WHEN( "used with via" ) {
      std::vector<std::string> values;
      auto deferred = pushmi::single_deferred([](auto out) {
        ::pushmi::set_value(out, 2.0);
        // ignored
        ::pushmi::set_value(out, 1);
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::min());
        ::pushmi::set_value(out, std::numeric_limits<int8_t>::max());
      });
      deferred | op::via([&](){return nt;}) |
          op::blocking_submit(v::on_value{[&](auto v) { values.push_back(std::to_string(v)); }});
      THEN( "only the first item was pushed" ) {
        REQUIRE(values == std::vector<std::string>{"2.000000"});
      }
    }
  }
}
