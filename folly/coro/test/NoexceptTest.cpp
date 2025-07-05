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

#include <folly/coro/AsyncScope.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Noexcept.h>
#include <folly/coro/safe/NowTask.h>
#include <folly/coro/safe/SafeTask.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

template <typename Awaitable>
auto co_fatalOnThrow(Awaitable awaitable) {
  return detail::NoexceptAwaitable<Awaitable, terminateOnCancel>(
      mustAwaitImmediatelyUnsafeMover(std::move(awaitable))());
}

// Check `await_result_t` for `NoexceptAwaitable`-wrapped awaitables.

static_assert(
    std::is_same_v<
        int,
        await_result_t<
            decltype(co_fatalOnThrow(FOLLY_DECLVAL(TaskWithExecutor<int>)))>>);
static_assert(std::is_same_v<
              int,
              await_result_t<decltype(co_fatalOnThrow(
                  FOLLY_DECLVAL(NowTaskWithExecutor<int>)))>>);

// Check whether `semi_await_result_t` is available for various value
// categories.  This is part of verifying that `NoexceptAwaitable` correctly
// preserves the immediately-awaitable property.

template <typename T, typename Res = int>
inline constexpr bool test_semi_await_result_v =
    std::is_same_v<detected_t<semi_await_result_t, T>, Res>;

static_assert(test_semi_await_result_v<Task<int>>);
static_assert(!test_semi_await_result_v<Task<int>&>);
static_assert(test_semi_await_result_v<Task<int>&&>);

using co_fatalOnThrow_of_Task =
    decltype(co_fatalOnThrow(FOLLY_DECLVAL(Task<int>)));
using co_fatalOnThrow_of_NowTask =
    decltype(co_fatalOnThrow(FOLLY_DECLVAL(NowTask<int>)));

static_assert(test_semi_await_result_v<co_fatalOnThrow_of_Task, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_Task&, int>);
static_assert(test_semi_await_result_v<co_fatalOnThrow_of_Task&&, int>);
static_assert(test_semi_await_result_v<co_fatalOnThrow_of_NowTask, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_NowTask&, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_NowTask&&, int>);

// Check that `noexcept_awaitable_v` is set as expected (via `co_awaitTry` and
// `NoexceptAwaitable`), and propagates even when wrapped.

static_assert(!noexcept_awaitable_v<Task<int>>);
static_assert(noexcept_awaitable_v<detail::TryAwaitable<Task<int>>>);
static_assert(noexcept_awaitable_v<co_fatalOnThrow_of_Task>);
static_assert(!noexcept_awaitable_v<detail::NothrowAwaitable<Task<int>>>);
static_assert(
    noexcept_awaitable_v<detail::NothrowAwaitable<co_fatalOnThrow_of_Task>>);

struct MyErr : std::exception {};

template <typename TaskT>
NowTask<void> checkFatalOnThrow() {
  auto coThrow = []() -> TaskT {
    throw MyErr{};
    co_return;
  };
  EXPECT_THROW(co_await coThrow(), MyErr);
  EXPECT_THROW(blockingWait(coThrow()), MyErr);
  EXPECT_DEATH({ blockingWait(co_fatalOnThrow(coThrow())); }, "MyErr");

  // Composition with `co_awaitTry()`.
  //
  // (1) The order `co_awaitTry(co_fatalOnThrow())` makes no sense, the
  // exception would fatal before getting to the `co_awaitTry`, and it shouldn't
  // compile since `NoexceptAwaiter` lacks `await_resume_try`.
  //
  // NB: If your metaprogramming task requires this for uniformity, you can of
  // course make it work, and adjust the test to be `EXPECT_DEATH`.
  static_assert(detail::is_awaitable_try<decltype(co_viaIfAsync(
                    FOLLY_DECLVAL(Executor::KeepAlive<>), coThrow()))>);
  static_assert(
      !detail::is_awaitable_try<decltype(co_viaIfAsync(
          FOLLY_DECLVAL(Executor::KeepAlive<>), co_fatalOnThrow(coThrow())))>);
  // (2) The opposite order "just works", no exception is thrown.
  auto ew = (co_await co_fatalOnThrow(co_awaitTry(coThrow()))).exception();
  EXPECT_NE(nullptr, ew.template get_exception<MyErr>());

  // Composition with `co_awaitTry()`.
  //
  // (1) Putting `co_fatalOnThrow` around `co_nothrow` doesn't compile because
  // `NothrowAwaitable` isn't an actual awaitable, and is special-cased in some
  // places.  This is fine, since it's unclear if any of the possible behaviors
  // for this combination are "expected" to the user.
  using NoexceptOfNothrow = decltype(co_fatalOnThrow(co_nothrow(coThrow())));
  static_assert(!test_semi_await_result_v<NoexceptOfNothrow, void>);
  static_assert(!is_awaitable_v<NoexceptOfNothrow>);
  static_assert(
      std::is_same_v<
          NoexceptOfNothrow,
          detail::NoexceptAwaitable<
              detail::NothrowAwaitable<TaskT>,
              terminateOnCancel>>);
  // ... but yes, this works
  static_assert(
      test_semi_await_result_v<decltype(co_fatalOnThrow(coThrow())), void>);
  // ... and yes, the problem is with `co_nothrow`
  static_assert(
      !test_semi_await_result_v<decltype(co_nothrow(coThrow())), void>);
  static_assert(!is_awaitable_v<decltype(co_nothrow(coThrow()))>);
  // (2) The other order isn't terribly useful, but it compiles, and works.
  auto awaitNothrowNoexcept = [&]() -> NowTask<void> {
    co_await co_nothrow(co_fatalOnThrow(coThrow()));
  };
  EXPECT_DEATH({ blockingWait(awaitNothrowNoexcept()); }, "MyErr");
}

CO_TEST(NoexceptTest, Task) {
  co_await checkFatalOnThrow<Task<void>>();
}

CO_TEST(NoexceptTest, NowTask) {
  co_await checkFatalOnThrow<NowTask<void>>();
}

// Test that `NowTask` remains immovable when wrapped in `co_fatalOnThrow`.

template <typename T>
using co_fatalOnThrow_result_t = decltype(co_fatalOnThrow(FOLLY_DECLVAL(T)));
// SFINAE check for whether `T` can be `co_fatalOnThrow`-wrapped.
// For `NowTask` we expect: prvalue -- yes, ref -- no.
template <typename T>
inline constexpr bool test_make_co_fatalOnThrow_v = std::is_same_v<
    detected_t<co_fatalOnThrow_result_t, T>,
    detail::NoexceptAwaitable<std::remove_reference_t<T>, terminateOnCancel>>;

CO_TEST(NoexceptTest, NowTaskIsImmediate) {
  auto myNowTask = []() -> NowTask<int> { co_return 5; };
  EXPECT_EQ(5, co_await co_fatalOnThrow(myNowTask()));

  static_assert(test_make_co_fatalOnThrow_v<Task<int>>);
  static_assert(!test_make_co_fatalOnThrow_v<Task<int>&>);
  static_assert(test_make_co_fatalOnThrow_v<Task<int>&&>);
  static_assert(test_make_co_fatalOnThrow_v<NowTask<int>>);
  static_assert(!test_make_co_fatalOnThrow_v<NowTask<int>&>);
  static_assert(!test_make_co_fatalOnThrow_v<NowTask<int>&&>);
#if 0 // The above asserts approximate this manual test
  auto t = myNowTask();
  co_fatalOnThrow(std::move(t));
#endif

  using MyNoexceptT = decltype(co_fatalOnThrow(FOLLY_DECLVAL(Task<int>)));
  static_assert(std::is_same_v<int, semi_await_result_t<MyNoexceptT>>);
  static_assert(!is_detected_v<semi_await_result_t, MyNoexceptT&>);
  static_assert(std::is_same_v<int, semi_await_result_t<MyNoexceptT&&>>);
  using MyNoexceptNowT = decltype(co_fatalOnThrow(myNowTask()));
  static_assert(std::is_same_v<int, semi_await_result_t<MyNoexceptNowT>>);
  static_assert(!is_detected_v<semi_await_result_t, MyNoexceptNowT&>);
  static_assert(!is_detected_v<semi_await_result_t, MyNoexceptNowT&&>);
#if 0 // The above asserts approximate this manual test
  auto t = co_fatalOnThrow(myNowTask());
  co_await std::move(t);
#endif
}

// Check `awaiter_type_t` and `await_result_t` for `AsNoexceptWithExecutor`

static_assert(
    std::is_same_v<
        detail::NoexceptAwaiter<TaskWithExecutor<void>, terminateOnCancel>,
        awaiter_type_t<AsNoexceptWithExecutor<
            TaskWithExecutor<void>,
            terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        detail::NoexceptAwaiter<NowTaskWithExecutor<void>, terminateOnCancel>,
        awaiter_type_t<AsNoexceptWithExecutor<
            NowTaskWithExecutor<void>,
            terminateOnCancel>>>);

static_assert(
    std::is_same_v<
        float,
        await_result_t<AsNoexceptWithExecutor<
            TaskWithExecutor<float>,
            terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        float,
        await_result_t<AsNoexceptWithExecutor<
            NowTaskWithExecutor<float>,
            terminateOnCancel>>>);

// Check whether `semi_await_result_t` is available for various value
// categories.  This is part of verifying that wrapping with `AsNoexcept<>`
// correctly preserves the immediately-awaitable property.

static_assert(
    test_semi_await_result_v<AsNoexcept<Task<int>, terminateOnCancel>, int>);
static_assert(
    !test_semi_await_result_v<AsNoexcept<Task<int>, terminateOnCancel>&, int>);
static_assert(
    test_semi_await_result_v<AsNoexcept<Task<int>, terminateOnCancel>&&, int>);
static_assert(
    test_semi_await_result_v<AsNoexcept<NowTask<int>, terminateOnCancel>, int>);
static_assert(!test_semi_await_result_v<
              AsNoexcept<NowTask<int>, terminateOnCancel>&,
              int>);
static_assert(!test_semi_await_result_v<
              AsNoexcept<NowTask<int>, terminateOnCancel>&&,
              int>);

// Check the `noexcept_awaitable_v` trait is applied correctly by `AsNoexcept`

static_assert(!noexcept_awaitable_v<Task<int>>);
static_assert(noexcept_awaitable_v<AsNoexcept<Task<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<NowTask<int>>);
static_assert(
    noexcept_awaitable_v<AsNoexcept<NowTask<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<TaskWithExecutor<int>>);
static_assert(
    noexcept_awaitable_v<
        AsNoexceptWithExecutor<TaskWithExecutor<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<NowTaskWithExecutor<int>>);
static_assert(
    noexcept_awaitable_v<
        AsNoexceptWithExecutor<NowTaskWithExecutor<int>, terminateOnCancel>>);

template <typename TaskT>
NowTask<void> checkAsNoexcept() {
  auto coFatalThrow = []() -> AsNoexcept<TaskT, terminateOnCancel> {
    throw MyErr{};
    co_return;
  };
  EXPECT_DEATH({ co_await coFatalThrow(); }, "MyErr");
  EXPECT_DEATH(
      {
        co_await co_withExecutor(co_await co_current_executor, coFatalThrow());
      },
      "MyErr");
}

CO_TEST(NoexceptTest, AsNoexceptTask) {
  co_await checkAsNoexcept<Task<void>>();

  // We want to check `AsNoexcept` for an `AsyncScope` task because
  // this uses a different code path to prepare the awaitable, specifically:
  //   co_withAsyncStack(yourTaskWithExecutor)
  auto coThrowFromScopeTask = []() -> NowTask<void> {
    AsyncScope scope{/*throwOnJoin*/ true};
    scope.add(co_withExecutor(
        co_await co_current_executor,
        []() -> AsNoexcept<Task<void>, terminateOnCancel> {
          throw MyErr{};
          co_return;
        }()));
    co_await scope.joinAsync();
  };
  EXPECT_DEATH(co_await coThrowFromScopeTask(), "MyErr");
}

CO_TEST(NoexceptTest, AsNoexceptNowTask) {
  co_await checkAsNoexcept<NowTask<void>>();
}

CO_TEST(NoexceptTest, AsNoexceptOnCancelVoid) {
  bool ran = false;
  auto coCancelSuccess = [&]() -> AsNoexcept<Task<>> {
    ran = true;
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return;
  };
  co_await coCancelSuccess();
  EXPECT_TRUE(ran);
}

CO_TEST(NoexceptTest, AsNoexceptOnCancelInt) {
  auto coCancelSuccess = [&]() -> AsNoexcept<Task<int>, OnCancel(42)> {
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return -1;
  };
  EXPECT_EQ(42, co_await coCancelSuccess());
}

// Spot-check the relevant `safe_alias_of` specializations
static_assert(
    safe_alias::unsafe_closure_internal ==
    safe_alias_of_v<detail::NoexceptAwaitable<
        SafeTask<safe_alias::unsafe_closure_internal>,
        OnCancel<void>{}>>);
static_assert(
    safe_alias::maybe_value ==
    safe_alias_of_v<detail::NoexceptAwaitable<
        SafeTask<safe_alias::maybe_value>,
        OnCancel<void>{}>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    safe_alias_of_v<AsNoexcept<SafeTask<safe_alias::unsafe_member_internal>>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    safe_alias_of_v<AsNoexceptWithExecutor<
        SafeTaskWithExecutor<safe_alias::unsafe_member_internal>>>);

} // namespace folly::coro

#endif
