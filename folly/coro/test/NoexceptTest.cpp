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
      // NOLINTNEXTLINE(facebook-folly-coro-temporary-by-ref)
      folly::ext::must_use_immediately_unsafe_mover(std::move(awaitable))());
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
                  FOLLY_DECLVAL(now_task_with_executor<int>)))>>);

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
using co_fatalOnThrow_of_now_task =
    decltype(co_fatalOnThrow(FOLLY_DECLVAL(now_task<int>)));

static_assert(test_semi_await_result_v<co_fatalOnThrow_of_Task, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_Task&, int>);
static_assert(test_semi_await_result_v<co_fatalOnThrow_of_Task&&, int>);
static_assert(test_semi_await_result_v<co_fatalOnThrow_of_now_task, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_now_task&, int>);
static_assert(!test_semi_await_result_v<co_fatalOnThrow_of_now_task&&, int>);

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
now_task<void> checkFatalOnThrow() {
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
  // NB: If your metaprogramming task requires this for uniformity, the good
  // path forward would be to ignore legacy `Try` and to instead add
  // `await_resume_result` returning `value_only_result`.  This way, you get
  // uniform UX without paying for the error path.  If implementing this, might
  // as well add an `EXPECT_DEATH` test too.
  static_assert(detail::is_awaitable_try<semi_await_awaitable_t<TaskT>>);
  static_assert(!detail::is_awaitable_try<
                semi_await_awaitable_t<decltype(co_fatalOnThrow(coThrow()))>>);
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
  auto awaitNothrowNoexcept = [&]() -> now_task<void> {
    co_await co_nothrow(co_fatalOnThrow(coThrow()));
  };
  EXPECT_DEATH({ blockingWait(awaitNothrowNoexcept()); }, "MyErr");
}

CO_TEST(NoexceptTest, Task) {
  co_await checkFatalOnThrow<Task<void>>();
}

CO_TEST(NoexceptTest, NowTask) {
  co_await checkFatalOnThrow<now_task<void>>();
}

// Test that `now_task` remains immovable when wrapped in `co_fatalOnThrow`.

template <typename T>
using co_fatalOnThrow_result_t = decltype(co_fatalOnThrow(FOLLY_DECLVAL(T)));
// SFINAE check for whether `T` can be `co_fatalOnThrow`-wrapped.
// For `now_task` we expect: prvalue -- yes, ref -- no.
template <typename T>
inline constexpr bool test_make_co_fatalOnThrow_v = std::is_same_v<
    detected_t<co_fatalOnThrow_result_t, T>,
    detail::NoexceptAwaitable<std::remove_reference_t<T>, terminateOnCancel>>;

CO_TEST(NoexceptTest, NowTaskIsImmediate) {
  auto myNowTask = []() -> now_task<int> { co_return 5; };
  EXPECT_EQ(5, co_await co_fatalOnThrow(myNowTask()));

  static_assert(test_make_co_fatalOnThrow_v<Task<int>>);
  static_assert(!test_make_co_fatalOnThrow_v<Task<int>&>);
  static_assert(test_make_co_fatalOnThrow_v<Task<int>&&>);
  static_assert(test_make_co_fatalOnThrow_v<now_task<int>>);
  static_assert(!test_make_co_fatalOnThrow_v<now_task<int>&>);
  static_assert(!test_make_co_fatalOnThrow_v<now_task<int>&&>);
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

// Check `awaiter_type_t` and `await_result_t` for `as_noexcept_with_executor`

static_assert(
    std::is_same_v<
        detail::NoexceptAwaiter<TaskWithExecutor<void>, terminateOnCancel>,
        awaiter_type_t<as_noexcept_with_executor<
            TaskWithExecutor<void>,
            terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        detail::
            NoexceptAwaiter<now_task_with_executor<void>, terminateOnCancel>,
        awaiter_type_t<as_noexcept_with_executor<
            now_task_with_executor<void>,
            terminateOnCancel>>>);

static_assert(
    std::is_same_v<
        float,
        await_result_t<as_noexcept_with_executor<
            TaskWithExecutor<float>,
            terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        float,
        await_result_t<as_noexcept_with_executor<
            now_task_with_executor<float>,
            terminateOnCancel>>>);

// Check whether `semi_await_result_t` is available for various value
// categories.  This is part of verifying that wrapping with `as_noexcept<>`
// correctly preserves the immediately-awaitable property.

static_assert(
    test_semi_await_result_v<as_noexcept<Task<int>, terminateOnCancel>, int>);
static_assert(
    !test_semi_await_result_v<as_noexcept<Task<int>, terminateOnCancel>&, int>);
static_assert(
    test_semi_await_result_v<as_noexcept<Task<int>, terminateOnCancel>&&, int>);
static_assert(test_semi_await_result_v<
              as_noexcept<now_task<int>, terminateOnCancel>,
              int>);
static_assert(!test_semi_await_result_v<
              as_noexcept<now_task<int>, terminateOnCancel>&,
              int>);
static_assert(!test_semi_await_result_v<
              as_noexcept<now_task<int>, terminateOnCancel>&&,
              int>);

// Check the `noexcept_awaitable_v` trait is applied correctly by `as_noexcept`

static_assert(!noexcept_awaitable_v<Task<int>>);
static_assert(noexcept_awaitable_v<as_noexcept<Task<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<now_task<int>>);
static_assert(
    noexcept_awaitable_v<as_noexcept<now_task<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<TaskWithExecutor<int>>);
static_assert(
    noexcept_awaitable_v<
        as_noexcept_with_executor<TaskWithExecutor<int>, terminateOnCancel>>);

static_assert(!noexcept_awaitable_v<now_task_with_executor<int>>);
static_assert(
    noexcept_awaitable_v<as_noexcept_with_executor<
        now_task_with_executor<int>,
        terminateOnCancel>>);

template <typename TaskT>
now_task<void> checkAsNoexcept() {
  auto coFatalThrow = []() -> as_noexcept<TaskT, terminateOnCancel> {
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

  // We want to check `as_noexcept` for an `AsyncScope` task because
  // this uses a different code path to prepare the awaitable, specifically:
  //   co_withAsyncStack(yourTaskWithExecutor)
  auto coThrowFromScopeTask = []() -> now_task<> {
    AsyncScope scope{/*throwOnJoin*/ true};
    scope.add(co_withExecutor(
        co_await co_current_executor,
        []() -> as_noexcept<Task<>, terminateOnCancel> {
          throw MyErr{};
          co_return;
        }()));
    co_await scope.joinAsync();
  };
  EXPECT_DEATH(co_await coThrowFromScopeTask(), "MyErr");
}

CO_TEST(NoexceptTest, AsNoexceptNowTask) {
  co_await checkAsNoexcept<now_task<>>();
}

CO_TEST(NoexceptTest, AsNoexceptOnCancelVoid) {
  bool ran = false;
  auto coCancelSuccess = [&]() -> as_noexcept<Task<>> {
    ran = true;
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return;
  };
  co_await coCancelSuccess();
  EXPECT_TRUE(ran);
}

CO_TEST(NoexceptTest, AsNoexceptOnCancelInt) {
  auto coCancelSuccess = [&]() -> as_noexcept<Task<int>, OnCancel(42)> {
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return -1;
  };
  EXPECT_EQ(42, co_await coCancelSuccess());
}

// Spot-check the relevant `safe_alias_of` specializations
static_assert(
    safe_alias::unsafe_closure_internal ==
    lenient_safe_alias_of_v<detail::NoexceptAwaitable<
        safe_task<safe_alias::unsafe_closure_internal>,
        OnCancel<void>{}>>);
static_assert(
    safe_alias::maybe_value ==
    strict_safe_alias_of_v<detail::NoexceptAwaitable<
        safe_task<safe_alias::maybe_value>,
        OnCancel<void>{}>>);
static_assert(
    safe_alias::maybe_value ==
    lenient_safe_alias_of_v<detail::NoexceptAwaitable<
        safe_task<safe_alias::maybe_value>,
        OnCancel<void>{}>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    lenient_safe_alias_of_v<
        as_noexcept<safe_task<safe_alias::unsafe_member_internal>>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    lenient_safe_alias_of_v<as_noexcept_with_executor<
        safe_task_with_executor<safe_alias::unsafe_member_internal>>>);

} // namespace folly::coro

#endif
