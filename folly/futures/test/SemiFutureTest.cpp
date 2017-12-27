/*
 * Copyright 2017 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/Executor.h>
#include <folly/Memory.h>
#include <folly/Unit.h>
#include <folly/dynamic.h>
#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <type_traits>

using namespace folly;

#define EXPECT_TYPE(x, T) EXPECT_TRUE((std::is_same<decltype(x), T>::value))

typedef FutureException eggs_t;
static eggs_t eggs("eggs");

// Future

TEST(SemiFuture, makeEmpty) {
  auto f = SemiFuture<int>::makeEmpty();
  EXPECT_THROW(f.isReady(), NoState);
}

TEST(SemiFuture, futureDefaultCtor) {
  SemiFuture<Unit>();
}

TEST(SemiFuture, makeSemiFutureWithUnit) {
  int count = 0;
  SemiFuture<Unit> fu = makeSemiFutureWith([&] { count++; });
  EXPECT_EQ(1, count);
}

namespace {
SemiFuture<int> onErrorHelperEggs(const eggs_t&) {
  return makeSemiFuture(10);
}
SemiFuture<int> onErrorHelperGeneric(const std::exception&) {
  return makeSemiFuture(20);
}
} // namespace

TEST(SemiFuture, special) {
  EXPECT_FALSE(std::is_copy_constructible<SemiFuture<int>>::value);
  EXPECT_FALSE(std::is_copy_assignable<SemiFuture<int>>::value);
  EXPECT_TRUE(std::is_move_constructible<SemiFuture<int>>::value);
  EXPECT_TRUE(std::is_move_assignable<SemiFuture<int>>::value);
}

TEST(SemiFuture, value) {
  auto f = makeSemiFuture(std::make_unique<int>(42));
  auto up = std::move(f.value());
  EXPECT_EQ(42, *up);

  EXPECT_THROW(makeSemiFuture<int>(eggs).value(), eggs_t);

  EXPECT_TYPE(std::declval<SemiFuture<int>&>().value(), int&);
  EXPECT_TYPE(std::declval<SemiFuture<int> const&>().value(), int const&);
  EXPECT_TYPE(std::declval<SemiFuture<int>&&>().value(), int&&);
  EXPECT_TYPE(std::declval<SemiFuture<int> const&&>().value(), int const&&);
}

TEST(SemiFuture, hasException) {
  EXPECT_TRUE(makeSemiFuture<int>(eggs).getTry().hasException());
  EXPECT_FALSE(makeSemiFuture(42).getTry().hasException());
}

TEST(SemiFuture, hasValue) {
  EXPECT_TRUE(makeSemiFuture(42).getTry().hasValue());
  EXPECT_FALSE(makeSemiFuture<int>(eggs).getTry().hasValue());
}

TEST(SemiFuture, makeSemiFuture) {
  EXPECT_TYPE(makeSemiFuture(42), SemiFuture<int>);
  EXPECT_EQ(42, makeSemiFuture(42).value());

  EXPECT_TYPE(makeSemiFuture<float>(42), SemiFuture<float>);
  EXPECT_EQ(42, makeSemiFuture<float>(42).value());

  auto fun = [] { return 42; };
  EXPECT_TYPE(makeSemiFutureWith(fun), SemiFuture<int>);
  EXPECT_EQ(42, makeSemiFutureWith(fun).value());

  auto funf = [] { return makeSemiFuture<int>(43); };
  EXPECT_TYPE(makeSemiFutureWith(funf), SemiFuture<int>);
  EXPECT_EQ(43, makeSemiFutureWith(funf).value());

  auto failfun = []() -> int { throw eggs; };
  EXPECT_TYPE(makeSemiFutureWith(failfun), SemiFuture<int>);
  EXPECT_NO_THROW(makeSemiFutureWith(failfun));
  EXPECT_THROW(makeSemiFutureWith(failfun).value(), eggs_t);

  auto failfunf = []() -> SemiFuture<int> { throw eggs; };
  EXPECT_TYPE(makeSemiFutureWith(failfunf), SemiFuture<int>);
  EXPECT_NO_THROW(makeSemiFutureWith(failfunf));
  EXPECT_THROW(makeSemiFutureWith(failfunf).value(), eggs_t);

  EXPECT_TYPE(makeSemiFuture(), SemiFuture<Unit>);
}

TEST(SemiFuture, Constructor) {
  auto f1 = []() -> SemiFuture<int> { return SemiFuture<int>(3); }();
  EXPECT_EQ(f1.value(), 3);
  auto f2 = []() -> SemiFuture<Unit> { return SemiFuture<Unit>(); }();
  EXPECT_NO_THROW(f2.value());
}

TEST(SemiFuture, ImplicitConstructor) {
  auto f1 = []() -> SemiFuture<int> { return 3; }();
  EXPECT_EQ(f1.value(), 3);
}

TEST(SemiFuture, InPlaceConstructor) {
  auto f = SemiFuture<std::pair<int, double>>(in_place, 5, 3.2);
  EXPECT_EQ(5, f.value().first);
}

TEST(SemiFuture, makeSemiFutureNoThrow) {
  makeSemiFuture().value();
}

TEST(SemiFuture, ViaThrowOnNull) {
  EXPECT_THROW(makeSemiFuture().via(nullptr), NoExecutor);
}

TEST(SemiFuture, ConstructSemiFutureFromEmptyFuture) {
  auto f = SemiFuture<int>{Future<int>::makeEmpty()};
  EXPECT_THROW(f.isReady(), NoState);
}

TEST(SemiFuture, ConstructSemiFutureFromFutureDefaultCtor) {
  SemiFuture<Unit>(Future<Unit>{});
}

TEST(SemiFuture, MakeSemiFutureFromFutureWithUnit) {
  int count = 0;
  SemiFuture<Unit> fu = SemiFuture<Unit>{makeFutureWith([&] { count++; })};
  EXPECT_EQ(1, count);
}

TEST(SemiFuture, MakeSemiFutureFromFutureWithValue) {
  auto f =
      SemiFuture<std::unique_ptr<int>>{makeFuture(std::make_unique<int>(42))};
  auto up = std::move(f.value());
  EXPECT_EQ(42, *up);
}

TEST(SemiFuture, MakeSemiFutureFromReadyFuture) {
  Promise<int> p;
  auto f = p.getSemiFuture();
  EXPECT_FALSE(f.isReady());
  p.setValue(42);
  EXPECT_TRUE(f.isReady());
}

TEST(SemiFuture, MakeSemiFutureFromNotReadyFuture) {
  Promise<int> p;
  auto f = p.getSemiFuture();
  EXPECT_THROW(f.value(), eggs_t);
}

TEST(SemiFuture, MakeFutureFromSemiFuture) {
  folly::EventBase e;
  Promise<int> p;
  std::atomic<int> result{0};
  auto f = p.getSemiFuture();
  auto future = std::move(f).via(&e).then([&](int value) {
    result = value;
    return value;
  });
  e.loop();
  EXPECT_EQ(result, 0);
  EXPECT_FALSE(future.isReady());
  p.setValue(42);
  e.loop();
  EXPECT_TRUE(future.isReady());
  ASSERT_EQ(future.value(), 42);
  ASSERT_EQ(result, 42);
}

TEST(SemiFuture, MakeFutureFromSemiFutureReturnFuture) {
  folly::EventBase e;
  Promise<int> p;
  int result{0};
  auto f = p.getSemiFuture();
  auto future = std::move(f).via(&e).then([&](int value) {
    result = value;
    return folly::makeFuture(std::move(value));
  });
  e.loop();
  EXPECT_EQ(result, 0);
  EXPECT_FALSE(future.isReady());
  p.setValue(42);
  e.loop();
  EXPECT_TRUE(future.isReady());
  ASSERT_EQ(future.value(), 42);
  ASSERT_EQ(result, 42);
}

TEST(SemiFuture, MakeFutureFromSemiFutureReturnSemiFuture) {
  folly::EventBase e;
  Promise<int> p;
  int result{0};
  auto f = p.getSemiFuture();
  auto future = std::move(f)
                    .via(&e)
                    .then([&](int value) {
                      result = value;
                      return folly::makeSemiFuture(std::move(value));
                    })
                    .then([&](int value) {
                      return folly::makeSemiFuture(std::move(value));
                    });
  e.loop();
  EXPECT_EQ(result, 0);
  EXPECT_FALSE(future.isReady());
  p.setValue(42);
  e.loop();
  EXPECT_TRUE(future.isReady());
  ASSERT_EQ(future.value(), 42);
  ASSERT_EQ(result, 42);
}

TEST(SemiFuture, MakeFutureFromSemiFutureLValue) {
  folly::EventBase e;
  Promise<int> p;
  std::atomic<int> result{0};
  auto f = p.getSemiFuture();
  auto future = std::move(f).via(&e).then([&](int value) {
    result = value;
    return value;
  });
  e.loop();
  EXPECT_EQ(result, 0);
  EXPECT_FALSE(future.isReady());
  p.setValue(42);
  e.loop();
  EXPECT_TRUE(future.isReady());
  ASSERT_EQ(future.value(), 42);
  ASSERT_EQ(result, 42);
}

TEST(SemiFuture, SimpleDefer) {
  std::atomic<int> innerResult{0};
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&]() { innerResult = 17; });
  p.setValue();
  // Run "F" here inline in the calling thread
  std::move(sf).get();
  ASSERT_EQ(innerResult, 17);
}

TEST(SemiFuture, DeferWithGetVia) {
  std::atomic<int> innerResult{0};
  EventBase e2;
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&]() { innerResult = 17; });
  p.setValue();
  std::move(sf).getVia(&e2);
  ASSERT_EQ(innerResult, 17);
}

TEST(SemiFuture, DeferWithVia) {
  std::atomic<int> innerResult{0};
  EventBase e2;
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&]() { innerResult = 17; });
  // Run "F" here inline in the calling thread
  auto tf = std::move(sf).via(&e2);
  p.setValue();
  tf.getVia(&e2);
  ASSERT_EQ(innerResult, 17);
}

TEST(SemiFuture, ChainingDefertoThen) {
  std::atomic<int> innerResult{0};
  std::atomic<int> result{0};
  EventBase e2;
  Promise<folly::Unit> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&]() { innerResult = 17; });
  // Run "F" here inline in a task running on the eventbase
  auto tf = std::move(sf).via(&e2).then([&]() { result = 42; });
  p.setValue();
  tf.getVia(&e2);
  ASSERT_EQ(innerResult, 17);
  ASSERT_EQ(result, 42);
}

TEST(SemiFuture, SimpleDeferWithValue) {
  std::atomic<int> innerResult{0};
  Promise<int> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&](int a) { innerResult = a; });
  p.setValue(7);
  // Run "F" here inline in the calling thread
  std::move(sf).get();
  ASSERT_EQ(innerResult, 7);
}

TEST(SemiFuture, ChainingDefertoThenWithValue) {
  std::atomic<int> innerResult{0};
  std::atomic<int> result{0};
  EventBase e2;
  Promise<int> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&](int a) {
    innerResult = a;
    return a;
  });
  // Run "F" here inline in a task running on the eventbase
  auto tf = std::move(sf).via(&e2).then([&](int a) { result = a; });
  p.setValue(7);
  tf.getVia(&e2);
  ASSERT_EQ(innerResult, 7);
  ASSERT_EQ(result, 7);
}

TEST(SemiFuture, MakeSemiFutureFromFutureWithTry) {
  Promise<int> p;
  auto f = p.getFuture();
  auto sf = std::move(f).semi().defer([&](Try<int> t) {
    if (auto err = t.tryGetExceptionObject<std::logic_error>()) {
      return Try<std::string>(err->what());
    }
    return Try<std::string>(
        make_exception_wrapper<std::logic_error>("Exception"));
  });
  p.setException(make_exception_wrapper<std::logic_error>("Try"));
  auto tryResult = std::move(sf).get();
  ASSERT_EQ(tryResult.value(), "Try");
}
