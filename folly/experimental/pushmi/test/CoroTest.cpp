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

#include <folly/Portability.h>
#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/experimental/pushmi/sender/single_sender.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace folly::pushmi::aliases;
using namespace testing;

namespace {
struct answer : mi::single_sender_tag::with_values<int> {
  PUSHMI_TEMPLATE(class To)(            //
    requires mi::ReceiveValue<To&, int> //
  )                                     //
  void submit(To to) {
    mi::set_value(to, 42);
    mi::set_done(to);
  }
};

PUSHMI_TEMPLATE(class From)( //
  requires mi::SingleTypedSender<From> //
) //
auto awaitSender(From from) ->
  folly::coro::detail::InlineTask<
    mi::sender_values_t<From, mi::detail::identity_t>>  {
  co_return co_await from;
}

folly::coro::detail::InlineTask<int> fetchAnswer() {
  co_return 42;
}
} // <anonymous> namespace

TEST(CoroTest, AwaitSender) {
  int ans = folly::coro::blockingWait(::awaitSender(answer{}));
  EXPECT_THAT(ans, Eq(42)) << "Expected the answer to be 42";
}

TEST(CoroTest, SubmitAwaitable) {
  int ans = 0;
  mi::submit(
    fetchAnswer(),
    mi::make_receiver([&](int j){ ans = j; }));
  EXPECT_THAT(ans, Eq(42)) << "Expected the answer to be 42";
}

#endif
