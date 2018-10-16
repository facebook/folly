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


SCENARIO( "empty can be used with tap and submit", "[empty][deferred]" ) {

  GIVEN( "An empty deferred" ) {
    auto e = op::empty();
    using E = decltype(e);

    REQUIRE( v::SenderTo<E, v::any_none<>, v::is_none<>> );

    WHEN( "tap and submit are applied" ) {
      int signals = 0;
      e |
        op::tap(
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; }) |
        op::submit(
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; });

      THEN( "the done signal is recorded twice" ) {
        REQUIRE( signals == 20 );
      }

      WHEN( "future_from is applied" ) {
        v::future_from(e).get();

        THEN( "future_from(e) returns std::future<void>" ) {
          REQUIRE( std::is_same<std::future<void>, decltype(v::future_from(e))>::value );
        }
      }
    }
  }

  GIVEN( "An empty int single_deferred" ) {
    auto e = op::empty<int>();
    using E = decltype(e);

    REQUIRE( v::SenderTo<E, v::any_single<int>, v::is_single<>> );

    WHEN( "tap and submit are applied" ) {
      
      int signals = 0;
      e |
        op::tap(
          [&](auto v){ signals += 100; },
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; }) |
        op::submit(
          [&](auto v){ signals += 100; },
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; });

      THEN( "the done signal is recorded twice" ) {
        REQUIRE( signals == 20 );
      }
    }
  }
}

SCENARIO( "just() can be used with transform and submit", "[just][deferred]" ) {

  GIVEN( "A just int single_deferred" ) {
    auto j = op::just(20);
    using J = decltype(j);

    REQUIRE( v::SenderTo<J, v::any_single<int>, v::is_single<>> );

    WHEN( "transform and submit are applied" ) {
      int signals = 0;
      int value = 0;
      j |
        op::transform(
          [&](int v){ signals += 10000; return v + 1; },
          [&](auto v){ std:abort(); return v; }) |
        op::transform(
          [&](int v){ signals += 10000; return v * 2; }) |
        op::submit(
          [&](auto v){ value = v; signals += 100; },
          [&](auto e) noexcept { signals += 1000; },
          [&](){ signals += 10; });

      THEN( "the transform signal is recorded twice, the value signal once and the result is correct" ) {
        REQUIRE( signals == 20100 );
        REQUIRE( value == 42 );
      }
    }

    WHEN( "future_from<int> is applied" ) {
      auto twenty = v::future_from<int>(j).get();

      THEN( "the value signal is recorded once and the result is correct" ) {
        REQUIRE( twenty == 20 );
        REQUIRE( std::is_same<std::future<int>, decltype(v::future_from<int>(j))>::value );
      }
    }
  }
}
