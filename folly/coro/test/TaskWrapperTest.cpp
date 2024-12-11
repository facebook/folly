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

#include <folly/coro/GtestHelpers.h>
#include <folly/coro/TaskWrapper.h>
#include <folly/coro/Timeout.h>
#include <folly/fibers/Semaphore.h>

using namespace std::literals::chrono_literals;

namespace folly::coro {

template <typename>
struct TinyTask;

namespace detail {
template <typename T>
class TinyTaskPromise final
    : public TaskPromiseWrapper<T, TinyTask<T>, TaskPromise<T>> {};
} // namespace detail

template <typename T>
struct FOLLY_CORO_TASK_ATTRS TinyTask final
    : public TaskWrapperCrtp<TinyTask<T>, T, Task<T>> {
  using promise_type = detail::TinyTaskPromise<T>;
  using TaskWrapperCrtp<TinyTask<T>, T, Task<T>>::TaskWrapperCrtp;
};

CO_TEST(TaskWrapper, trivial) {
  EXPECT_EQ(
      1337, co_await [](int x) -> TinyTask<int> { co_return 1300 + x; }(37));
}

namespace {
TinyTask<int> intFunc(auto x) {
  co_return *x;
}
} // namespace

CO_TEST(TaskWrapper, returnsNonVoid) {
  auto x = std::make_unique<int>(17);
  auto lambdaTmpl = [](auto x) -> TinyTask<int> { co_return x; };
  EXPECT_EQ(20, co_await intFunc(std::move(x)) + co_await lambdaTmpl(3));
}

namespace {
TinyTask<void> voidFunc(auto x, int* ran) {
  EXPECT_EQ(17, *x);
  ++*ran;
  co_return;
}
} // namespace

CO_TEST(TaskWrapper, returnsVoidLambda) {
  int ran = 0;
  auto lambdaTmpl = [&](auto x) -> TinyTask<void> {
    EXPECT_EQ(3, x);
    ++ran;
    co_return;
  };
  co_await lambdaTmpl(3);
  EXPECT_EQ(1, ran);
}

CO_TEST(TaskWrapper, returnsVoidFn) {
  int ran = 0;
  auto x = std::make_unique<int>(17);
  co_await voidFunc(std::move(x), &ran);
  EXPECT_EQ(1, ran);
}

CO_TEST(TaskWrapper, awaitsTask) {
  EXPECT_EQ(
      1337, co_await []() -> TinyTask<int> {
        co_return 1300 + co_await ([]() -> Task<int> { co_return 37; }());
      }());
}

CO_TEST(TaskWrapper, cancellation) {
  bool ran = false;
  EXPECT_THROW(
      co_await timeout(
          [&]() -> TinyTask<void> {
            ran = true;
            folly::fibers::Semaphore stuck{0}; // a cancellable baton
            co_await stuck.co_wait();
          }(),
          200ms),
      folly::FutureTimeout);
  EXPECT_TRUE(ran);
}

namespace {
struct MyError : std::exception {};
} // namespace

CO_TEST(TaskWrapper, throws) {
  EXPECT_THROW(
      co_await []() -> TinyTask<void> { co_yield co_error(MyError{}); }(),
      MyError);
}

CO_TEST(TaskWrapper, co_awaitTry) {
  auto res = co_await co_awaitTry([]() -> TinyTask<void> {
    co_yield co_error(MyError{});
  }());
  EXPECT_TRUE(res.hasException<MyError>());
}

CO_TEST(TaskWrapper, returnImplicitCtor) {
  auto t = []() -> TinyTask<std::pair<int, int>> { co_return {3, 4}; };
  EXPECT_EQ(std::pair(3, 4), co_await t());
}

template <typename, typename, typename>
struct RecursiveWrapTask;

namespace detail {
template <typename T, typename InnerSemiAwaitable, typename InnerPromise>
class RecursiveWrapTaskPromise final
    : public TaskPromiseWrapper<
          T,
          RecursiveWrapTask<T, InnerSemiAwaitable, InnerPromise>,
          InnerPromise> {};
} // namespace detail

template <typename T, typename InnerSemiAwaitable, typename InnerPromise>
struct RecursiveWrapTask final
    : public TaskWrapperCrtp<
          RecursiveWrapTask<T, InnerSemiAwaitable, InnerPromise>,
          T,
          InnerSemiAwaitable> {
  using promise_type =
      detail::RecursiveWrapTaskPromise<T, InnerSemiAwaitable, InnerPromise>;
  using TaskWrapperCrtp<
      RecursiveWrapTask<T, InnerSemiAwaitable, InnerPromise>,
      T,
      InnerSemiAwaitable>::TaskWrapperCrtp;
  using TaskWrapperCrtp<
      RecursiveWrapTask<T, InnerSemiAwaitable, InnerPromise>,
      T,
      InnerSemiAwaitable>::unwrap;
};

template <typename T>
using TwoWrapTask =
    RecursiveWrapTask<T, TinyTask<T>, detail::TinyTaskPromise<T>>;
template <typename T>
using TwoWrapTaskPromise = detail::
    RecursiveWrapTaskPromise<T, TinyTask<T>, detail::TinyTaskPromise<T>>;

template <typename T>
using ThreeWrapTask =
    RecursiveWrapTask<T, TwoWrapTask<T>, TwoWrapTaskPromise<T>>;
template <typename T>
using ThreeWrapTaskPromise =
    detail::RecursiveWrapTaskPromise<T, TwoWrapTask<T>, TwoWrapTaskPromise<T>>;

CO_TEST(TaskWrapper, recursiveUnwrap) {
  auto t = []() -> ThreeWrapTask<int> { co_return 3; };
  EXPECT_EQ(3, co_await t());
  static_assert(std::is_same_v<decltype(t().unwrap()), TwoWrapTask<int>>);
  EXPECT_EQ(3, co_await t().unwrap());
  static_assert(std::is_same_v<decltype(t().unwrap().unwrap()), TinyTask<int>>);
  EXPECT_EQ(3, co_await t().unwrap().unwrap());
}

template <typename>
struct OpaqueTask;

namespace detail {
template <typename T>
class OpaqueTaskPromise final
    : public TaskPromiseWrapper<T, OpaqueTask<T>, TaskPromise<T>> {};
} // namespace detail

template <typename T>
struct FOLLY_CORO_TASK_ATTRS OpaqueTask final
    : public OpaqueTaskWrapperCrtp<OpaqueTask<T>, T, Task<T>> {
  using promise_type = detail::OpaqueTaskPromise<T>;
  using OpaqueTaskWrapperCrtp<OpaqueTask<T>, T, Task<T>>::OpaqueTaskWrapperCrtp;
  using OpaqueTaskWrapperCrtp<OpaqueTask<T>, T, Task<T>>::unwrap;
};

static_assert(is_semi_awaitable_v<TinyTask<int>>);
static_assert(!is_semi_awaitable_v<OpaqueTask<int>>);

CO_TEST(TaskWrapper, opaque) {
  auto ot = [](int x) -> OpaqueTask<int> { co_return 1300 + x; }(37);
  EXPECT_EQ(1337, co_await std::move(ot).unwrap());
}

} // namespace folly::coro
