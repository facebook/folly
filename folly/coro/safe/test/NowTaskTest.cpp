/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/safe/NowTask.h>

#if FOLLY_HAS_IMMOVABLE_COROUTINES

namespace folly::coro {

template <typename T>
constexpr T decl_prvalue();

Task<int> demoTask(int x) {
  co_return 1300 + x;
}
NowTask<int> demoNowTask(int x) {
  co_return 1300 + x;
}

template <typename T, typename Res = int>
inline constexpr bool test_semi_await_result_v =
    std::is_same_v<detected_t<semi_await_result_t, T>, Res>;

static_assert(test_semi_await_result_v<Task<int>>);
static_assert(test_semi_await_result_v<Task<int>&&>);
static_assert(test_semi_await_result_v<NowTask<int>>);
// FIXME: See next diff
static_assert(test_semi_await_result_v<NowTask<int>&&>);

using DemoTryTask = decltype(co_awaitTry(demoTask(37)));
using DemoTryNowTask = decltype(co_awaitTry(demoNowTask(37)));
static_assert(test_semi_await_result_v<DemoTryTask, Try<int>>);
static_assert(test_semi_await_result_v<DemoTryTask&&, Try<int>>);
static_assert(test_semi_await_result_v<DemoTryNowTask, Try<int>>);
// FIXME: See next diff
static_assert(test_semi_await_result_v<DemoTryNowTask&&, Try<int>>);

// Note: This `test_` predicate, and similar ones below, may look somewhat
// redundant with `test_semi_await_result_v` above.  Both aim to test this:
//   co_await demoNowTask(37); // works
//   auto t = demoNowTask(37);
//   co_await std::move(t); // does not compile
// We check against test bugs by ensuring that BOTH forms work for `Task`.
//
// The rationale for the supposed redundancy is that here, we spell out the
// expected call-path-to-awaiter.  This is pretty robust, so long as I check
// both `Task` (good) and `NowTask` (bad), whereas with the fancy
// metaprogramming of `semi_await_result_t`, there's more risk that
// `co_await std::move(t)` compiles for `NowTask`, even when the type
// function fails to substitute.
template <typename T>
using await_transform_result_t =
    decltype(std::declval<detail::TaskPromise<void>>().await_transform(
        FOLLY_DECLVAL(T)));
template <typename T>
inline constexpr bool test_transform_moved_v = std::is_same_v<
    detected_t<await_transform_result_t, T>,
    typename Task<int>::PrivateAwaiterTypeForTests>;

CO_TEST(NowTaskTest, simple) {
  EXPECT_EQ(1337, co_await demoNowTask(37));

  using T = Task<int>;
  static_assert(
      std::is_same<
          decltype(std::declval<detail::TaskPromise<void>>().await_transform(
              decl_prvalue<T>())),
          typename Task<int>::PrivateAwaiterTypeForTests>::value);

  static_assert(test_transform_moved_v<Task<int>>);
  static_assert(test_transform_moved_v<Task<int>&&>);
  static_assert(test_transform_moved_v<NowTask<int>>);
  // FIXME: See next diff
  static_assert(test_transform_moved_v<NowTask<int>&&>);
#if 1 // The above asserts are a proxy for this manual test
  auto t = demoNowTask(37);
  co_await std::move(t);
#endif
}

// `std::invoke_result_t` cannot pass prvalues -- it invokes a move ctor.
template <typename T>
using blockingWait_result_t = decltype(blockingWait(FOLLY_DECLVAL(T)));
template <typename T, typename Res>
inline constexpr bool test_blocking_wait_moved_v =
    std::is_same_v<detected_t<blockingWait_result_t, T>, Res>;

TEST(NowTaskTest, blockingWait) {
  EXPECT_EQ(1337, blockingWait(demoNowTask(37)));

  static_assert(test_blocking_wait_moved_v<Task<int>, int>);
  static_assert(test_blocking_wait_moved_v<Task<int>&&, int>);
  static_assert(test_blocking_wait_moved_v<NowTask<int>, int>);
  // FIXME: See next diff
  static_assert(test_blocking_wait_moved_v<NowTask<int>&&, int>);
#if 1 // The above asserts are a proxy for this manual test
  auto t = demoNowTask(37);
  blockingWait(std::move(t));
#endif
}

// Both of these are antipatterns with `Task` because if you awaited either
// of these coros outside of the statement that created them, it would have
// dangling refs.
//
// Since `NowTask` tries to ensure it can ONLY be awaited in the statement
// that created it, C++ lifetime extension should save our bacon.
CO_TEST(NowTaskTest, passByRef) {
  auto res = co_await [](int&& x) -> NowTask<int> { co_return 1300 + x; }(37);
  EXPECT_EQ(1337, res);
}
CO_TEST(NowTaskTest, lambdaWithCaptures) {
  int a = 1300, b = 37;
  auto res = co_await [&a, b]() -> NowTask<int> { co_return a + b; }();
  EXPECT_EQ(1337, res);
}

CO_TEST(NowTaskTest, toNowTask) {
  static_assert(
      std::is_same_v<NowTask<int>, decltype(toNowTask(demoNowTask(5)))>);
  auto t = []() -> Task<int> { co_return 5; }();
  static_assert(
      std::is_same_v<NowTask<int>, decltype(toNowTask(std::move(t)))>);
  EXPECT_EQ(5, co_await toNowTask(std::move(t)));
}

} // namespace folly::coro

#endif
