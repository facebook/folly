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

#include <glog/logging.h>

#include <folly/coro/AsyncScope.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/ValueOrFatal.h>
#include <folly/coro/safe/NowTask.h>
#include <folly/coro/safe/SafeTask.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

// Helper for fatal-on-any-exception wrapping of awaitables
template <typename Awaitable>
auto fatal_on_non_value(Awaitable awaitable) {
  return detail::ValueOrFatalAwaitable<
      Awaitable,
      on_stopped_and_error<will_fatal>>(
      folly::ext::must_use_immediately_unsafe_mover(std::move(awaitable))());
}

// Check `await_result_t` for `ValueOrFatalAwaitable`-wrapped awaitables.
static_assert(std::is_same_v<
              int,
              await_result_t<decltype(fatal_on_non_value(
                  FOLLY_DECLVAL(TaskWithExecutor<int>)))>>);
static_assert(std::is_same_v<
              int,
              await_result_t<decltype(fatal_on_non_value(
                  FOLLY_DECLVAL(now_task_with_executor<int>)))>>);

// Check whether `semi_await_result_t` is available for various value
// categories.  This is part of verifying that `ValueOrFatalAwaitable` correctly
// preserves the immediately-awaitable property.
template <typename T, typename Res = int>
inline constexpr bool test_semi_await_result_v =
    std::is_same_v<detected_t<semi_await_result_t, T>, Res>;

static_assert(test_semi_await_result_v<Task<int>>);
static_assert(!test_semi_await_result_v<Task<int>&>);
static_assert(test_semi_await_result_v<Task<int>&&>);

using fatal_on_non_value_of_Task =
    decltype(fatal_on_non_value(FOLLY_DECLVAL(Task<int>)));
using fatal_on_non_value_of_now_task =
    decltype(fatal_on_non_value(FOLLY_DECLVAL(now_task<int>)));

static_assert(test_semi_await_result_v<fatal_on_non_value_of_Task, int>);
static_assert(!test_semi_await_result_v<fatal_on_non_value_of_Task&, int>);
static_assert(test_semi_await_result_v<fatal_on_non_value_of_Task&&, int>);
static_assert(test_semi_await_result_v<fatal_on_non_value_of_now_task, int>);
static_assert(!test_semi_await_result_v<fatal_on_non_value_of_now_task&, int>);
static_assert(!test_semi_await_result_v<fatal_on_non_value_of_now_task&&, int>);

// Check that `value_only_awaitable_v` is set as expected (via `co_awaitTry` and
// `ValueOrFatalAwaitable`), and propagates even when wrapped.
static_assert(!value_only_awaitable_v<Task<int>>);
static_assert(value_only_awaitable_v<detail::TryAwaitable<Task<int>>>);
static_assert(value_only_awaitable_v<fatal_on_non_value_of_Task>);
static_assert(!value_only_awaitable_v<detail::NothrowAwaitable<Task<int>>>);

struct MyErr : std::exception {};

template <typename TaskT>
now_task<void> checkFatalOnNonValue() {
  auto coThrow = []() -> TaskT {
    throw MyErr{};
    co_return;
  };
  EXPECT_THROW(co_await coThrow(), MyErr);
  EXPECT_THROW(blockingWait(coThrow()), MyErr);
  EXPECT_DEATH({ blockingWait(fatal_on_non_value(coThrow())); }, "MyErr");

  // Composition with `co_awaitTry()`.
  //
  // (1) The order `co_awaitTry(fatal_on_non_value())` makes no sense, the
  // exception would fatal before getting to the `co_awaitTry`, and it shouldn't
  // compile since `ValueOrFatalAwaiter` lacks `await_resume_try`.
  //
  // NB: If your metaprogramming task requires this for uniformity, the good
  // path forward would be to ignore legacy `Try` and to instead add
  // `await_resume_result` returning `value_only_result`.  This way, you get
  // uniform UX without paying for the error path.  If implementing this, might
  // as well add an `EXPECT_DEATH` test too.
  static_assert(detail::is_awaitable_try<semi_await_awaitable_t<TaskT>>);
  static_assert(
      !detail::is_awaitable_try<
          semi_await_awaitable_t<decltype(fatal_on_non_value(coThrow()))>>);
  // (2) The opposite order "just works", no exception is thrown.
  auto ew = (co_await fatal_on_non_value(co_awaitTry(coThrow()))).exception();
  EXPECT_NE(nullptr, ew.template get_exception<MyErr>());

  // Composition with `co_nothrow()`.
  //
  // (1) Putting `fatal_on_non_value` around `co_nothrow` doesn't compile
  // because `NothrowAwaitable` isn't an actual awaitable, and is special-cased
  // in some places.  This is fine, since it's unclear if any of the possible
  // behaviors for this combination are "expected" to the user.
  using ValueOrFatalOfNothrow =
      decltype(fatal_on_non_value(co_nothrow(coThrow())));
  static_assert(!test_semi_await_result_v<ValueOrFatalOfNothrow, void>);
  static_assert(!is_awaitable_v<ValueOrFatalOfNothrow>);
  static_assert(
      std::is_same_v<
          ValueOrFatalOfNothrow,
          detail::ValueOrFatalAwaitable<
              detail::NothrowAwaitable<TaskT>,
              on_stopped_and_error<will_fatal>>>);
  // ... but yes, this works
  static_assert(
      test_semi_await_result_v<decltype(fatal_on_non_value(coThrow())), void>);
  // ... and yes, the problem is with `co_nothrow`
  static_assert(
      !test_semi_await_result_v<decltype(co_nothrow(coThrow())), void>);
  static_assert(!is_awaitable_v<decltype(co_nothrow(coThrow()))>);
  // (2) The reverse order is banned for reasons `NothrowAwaitable` describes
  bool ran = [&]<typename T>(T) { // `requires` does SFINAE inside templates
    (void)co_nothrow(coThrow()); // compiles
    (void)fatal_on_non_value(coThrow()); // compiles
    static_assert(requires { co_nothrow(coThrow()); }); // same as prior line
    static_assert(!requires { co_nothrow(fatal_on_non_value(coThrow())); });
#if 0 // manual test equivalent of above `static_assert`
    (void)co_nothrow(fatal_on_non_value(coThrow())); // constraint failure
#endif
    return true;
  }(5);
  EXPECT_TRUE(ran); // it's easy to forget to call the lambda
}

CO_TEST(ValueOrFatalTest, Task) {
  co_await checkFatalOnNonValue<Task<void>>();
}

CO_TEST(ValueOrFatalTest, NowTask) {
  co_await checkFatalOnNonValue<now_task<void>>();
}

// Test that `now_task` remains immovable when wrapped in `fatal_on_non_value`.
template <typename T>
using fatal_on_non_value_result_t =
    decltype(fatal_on_non_value(FOLLY_DECLVAL(T)));
// SFINAE check for whether `T` can be `fatal_on_non_value`-wrapped.
// For `now_task` we expect: prvalue -- yes, ref -- no.
template <typename T>
inline constexpr bool test_make_fatal_on_non_value_v = std::is_same_v<
    detected_t<fatal_on_non_value_result_t, T>,
    detail::ValueOrFatalAwaitable<
        std::remove_reference_t<T>,
        on_stopped_and_error<will_fatal>>>;

CO_TEST(ValueOrFatalTest, NowTaskIsImmediate) {
  auto myNowTask = []() -> now_task<int> { co_return 5; };
  EXPECT_EQ(5, co_await fatal_on_non_value(myNowTask()));

  static_assert(test_make_fatal_on_non_value_v<Task<int>>);
  static_assert(!test_make_fatal_on_non_value_v<Task<int>&>);
  static_assert(test_make_fatal_on_non_value_v<Task<int>&&>);
  static_assert(test_make_fatal_on_non_value_v<now_task<int>>);
  static_assert(!test_make_fatal_on_non_value_v<now_task<int>&>);
  static_assert(!test_make_fatal_on_non_value_v<now_task<int>&&>);
#if 0 // The above asserts approximate this manual test
  auto t = myNowTask();
  fatal_on_non_value(std::move(t));
#endif

  using MyValueOrFatalT =
      decltype(fatal_on_non_value(FOLLY_DECLVAL(Task<int>)));
  static_assert(std::is_same_v<int, semi_await_result_t<MyValueOrFatalT>>);
  static_assert(!is_detected_v<semi_await_result_t, MyValueOrFatalT&>);
  static_assert(std::is_same_v<int, semi_await_result_t<MyValueOrFatalT&&>>);
  using MyValueOrFatalNowT = decltype(fatal_on_non_value(myNowTask()));
  static_assert(std::is_same_v<int, semi_await_result_t<MyValueOrFatalNowT>>);
  static_assert(!is_detected_v<semi_await_result_t, MyValueOrFatalNowT&>);
  static_assert(!is_detected_v<semi_await_result_t, MyValueOrFatalNowT&&>);
#if 0 // The above asserts approximate this manual test
  auto t = fatal_on_non_value(myNowTask());
  co_await std::move(t);
#endif
}

// Check `awaiter_type_t` and `await_result_t` for `value_or_fatal`.
template <typename Inner, typename Res = void>
consteval bool check_value_or_fatal_awaiter() {
  static_assert(
      std::is_same_v<
          detail::ValueOrFatalAwaiter<Inner, on_stopped_and_error<will_fatal>>,
          awaiter_type_t<
              value_or_fatal<Inner, on_stopped_and_error<will_fatal>>>>);
  static_assert(
      std::is_same_v<
          Res,
          await_result_t<
              value_or_fatal<Inner, on_stopped_and_error<will_fatal>>>>);
  return true;
}
static_assert(check_value_or_fatal_awaiter<TaskWithExecutor<void>>());
static_assert(check_value_or_fatal_awaiter<now_task_with_executor<void>>());
static_assert(check_value_or_fatal_awaiter<TaskWithExecutor<float>, float>());
static_assert(
    check_value_or_fatal_awaiter<now_task_with_executor<float>, float>());

// Check whether `semi_await_result_t` is available for various value
// categories.  This is part of verifying that wrapping with `value_or_fatal<>`
// correctly preserves the immediately-awaitable property.
static_assert(test_semi_await_result_v<
              value_or_fatal<Task<int>, on_stopped_and_error<will_fatal>>,
              int>);
static_assert(!test_semi_await_result_v<
              value_or_fatal<Task<int>, on_stopped_and_error<will_fatal>>&,
              int>);
static_assert(test_semi_await_result_v<
              value_or_fatal<Task<int>, on_stopped_and_error<will_fatal>>&&,
              int>);
static_assert(test_semi_await_result_v<
              value_or_fatal<now_task<int>, on_stopped_and_error<will_fatal>>,
              int>);
static_assert(!test_semi_await_result_v<
              value_or_fatal<now_task<int>, on_stopped_and_error<will_fatal>>&,
              int>);
static_assert(!test_semi_await_result_v<
              value_or_fatal<now_task<int>, on_stopped_and_error<will_fatal>>&&,
              int>);

// Check the `value_only_awaitable_v` trait is applied correctly by
// `value_or_fatal`
static_assert(!value_only_awaitable_v<Task<int>>);
static_assert(value_only_awaitable_v<
              value_or_fatal<Task<int>, on_stopped_and_error<will_fatal>>>);
static_assert(!value_only_awaitable_v<now_task<int>>);
static_assert(value_only_awaitable_v<
              value_or_fatal<now_task<int>, on_stopped_and_error<will_fatal>>>);
static_assert(!value_only_awaitable_v<TaskWithExecutor<int>>);
static_assert(
    value_only_awaitable_v<value_or_fatal<
        TaskWithExecutor<int>,
        on_stopped_and_error<will_fatal>>>);
static_assert(!value_only_awaitable_v<now_task_with_executor<int>>);
static_assert(
    value_only_awaitable_v<value_or_fatal<
        now_task_with_executor<int>,
        on_stopped_and_error<will_fatal>>>);

// Test on_stopped_and_error<will_fatal>: both stopped and error terminate
template <typename TaskT>
now_task<void> checkValueOrFatalAllFatal() {
  // Error path terminates
  auto coFatalThrow =
      []() -> value_or_fatal<TaskT, on_stopped_and_error<will_fatal>> {
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

CO_TEST(ValueOrFatalTest, AllFatalTask) {
  co_await checkValueOrFatalAllFatal<Task<void>>();

  // We want to check `value_or_fatal` for an `AsyncScope` task because
  // this uses a different code path to prepare the awaitable, specifically:
  //   co_withAsyncStack(yourTaskWithExecutor)
  auto coThrowFromScopeTask = []() -> now_task<> {
    AsyncScope scope{/*throwOnJoin*/ true};
    scope.add(co_withExecutor(
        co_await co_current_executor,
        []() -> value_or_fatal<Task<>, on_stopped_and_error<will_fatal>> {
          throw MyErr{};
          co_return;
        }()));
    co_await scope.joinAsync();
  };
  EXPECT_DEATH(co_await coThrowFromScopeTask(), "MyErr");
}

CO_TEST(ValueOrFatalTest, AllFatalNowTask) {
  co_await checkValueOrFatalAllFatal<now_task<>>();
}

CO_TEST(ValueOrFatalTest, OnStoppedVoid) {
  bool ran = false;
  auto coCancelSuccess = [&]() -> value_or_fatal<Task<>, on_stopped_void> {
    ran = true;
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return;
  };
  co_await coCancelSuccess();
  EXPECT_TRUE(ran);
}

CO_TEST(ValueOrFatalTest, OnStoppedInt) {
  auto coCancelSuccess = [&]() -> value_or_fatal<Task<int>, on_stopped<42>> {
    throw OperationCancelled{}; // pretend to be cancelled
    LOG(FATAL) << "not reached";
    co_return -1;
  };
  EXPECT_EQ(42, co_await coCancelSuccess());
}

// co_withExecutor(as_noexcept<Task>) makes as_noexcept<TaskWithExecutor>
static_assert(
    std::is_same_v<
        value_or_fatal<TaskWithExecutor<int>, on_stopped_and_error<will_fatal>>,
        decltype(co_withExecutor(
            FOLLY_DECLVAL(Executor::KeepAlive<>),
            FOLLY_DECLVAL(
                value_or_fatal<
                    Task<int>,
                    on_stopped_and_error<will_fatal>>)))>);

// Spot-check the relevant `safe_alias_of` specializations
static_assert(
    safe_alias::unsafe_closure_internal ==
    lenient_safe_alias_of_v<detail::ValueOrFatalAwaitable<
        safe_task<safe_alias::unsafe_closure_internal>,
        on_stopped<unit>>>);
static_assert(
    safe_alias::maybe_value ==
    strict_safe_alias_of_v<detail::ValueOrFatalAwaitable<
        safe_task<safe_alias::maybe_value>,
        on_stopped<unit>>>);
static_assert(
    safe_alias::maybe_value ==
    lenient_safe_alias_of_v<detail::ValueOrFatalAwaitable<
        safe_task<safe_alias::maybe_value>,
        on_stopped<unit>>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    lenient_safe_alias_of_v<value_or_fatal<
        safe_task<safe_alias::unsafe_member_internal>,
        on_stopped_void>>);
static_assert(
    safe_alias::unsafe_member_internal ==
    lenient_safe_alias_of_v<value_or_fatal<
        safe_task_with_executor<safe_alias::unsafe_member_internal>,
        on_stopped_void>>);

} // namespace folly::coro

#endif
