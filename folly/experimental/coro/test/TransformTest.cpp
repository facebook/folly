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

#include <utility>
#include <folly/Portability.h>

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Transform.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

class TransformTest : public testing::Test {};

AsyncGenerator<int> ints(int n) {
  for (int i = 0; i <= n; ++i) {
    co_await co_reschedule_on_current_executor;
    co_yield i;
  }
}

TEST_F(TransformTest, SimpleStream) {
  struct MyError : std::exception {};

  const float seenEndOfStream = 100.0f;
  const float seenError = 200.0f;

  std::vector<float> lastSeen(3, -1);
  std::vector<float> expectedSeen = {
      {seenEndOfStream, seenEndOfStream, seenError}};

  int totalEventCount = 0;

  auto selectStream = [](int index) -> AsyncGenerator<int> {
    auto failing = []() -> AsyncGenerator<int> {
      co_yield 0;
      co_yield 1;
      throw MyError{};
    };

    if (index == 0) {
      return ints(4);
    } else if (index == 1) {
      return ints(3);
    }
    return failing();
  };

  auto test = [&](int index) -> Task<void> {
    auto generator =
        transform(selectStream(index), [](int i) { return i * 1.0f; });
    try {
      while (auto item = co_await generator.next()) {
        ++totalEventCount;

        auto value = *item;
        CHECK(index >= 0 && index <= 2);
        CHECK_EQ(lastSeen[index] + 1.0f, value);
        lastSeen[index] = value;
      }

      // EndOfStream
      if (index == 0) {
        CHECK_EQ(4, lastSeen[index]);
      } else if (index == 1) {
        CHECK_EQ(3, lastSeen[index]);
      } else {
        // Stream 2 should have completed with an error not EndOfStream.
        CHECK(false);
      }
      lastSeen[index] = seenEndOfStream;

    } catch (const MyError&) {
      // Error
      CHECK_EQ(2, index);
      CHECK_EQ(1, lastSeen[index]);
      lastSeen[index] = seenError;
    }

    CHECK_EQ(expectedSeen[index], lastSeen[index]);
  };
  blockingWait(test(0));
  blockingWait(test(1));
  blockingWait(test(2));
}

template <typename Ref, typename Value = folly::remove_cvref_t<Ref>>
folly::coro::AsyncGenerator<Ref, Value> neverStream() {
  folly::coro::Baton baton;
  folly::CancellationCallback cb{
      co_await folly::coro::co_current_cancellation_token,
      [&] { baton.post(); }};
  co_await baton;
}

TEST_F(TransformTest, CancellationTokenPropagatesFromConsumer) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    bool suspended = false;
    bool done = false;
    co_await folly::coro::collectAll(
        folly::coro::co_withCancellation(
            cancelSource.getToken(),
            [&]() -> folly::coro::Task<void> {
              auto stream =
                  transform(neverStream<float>(), [](float) { return 42; });
              suspended = true;
              auto result = co_await stream.next();
              CHECK(!result.has_value());
              done = true;
            }()),
        [&]() -> folly::coro::Task<void> {
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          co_await folly::coro::co_reschedule_on_current_executor;
          CHECK(suspended);
          CHECK(!done);
          cancelSource.requestCancellation();
        }());
    CHECK(done);
  }());
}

AsyncGenerator<float&> floatRefs(int n) {
  for (int i = 0; i <= n; ++i) {
    co_await co_reschedule_on_current_executor;
    float f = i;
    co_yield f;
  }
}

template <typename T>
auto useStream(AsyncGenerator<T> ag) {
  std::vector<typename decltype(ag)::value_type> vals;
  blockingWait([&](auto ag) -> Task<void> {
    while (auto item = co_await ag.next()) {
      vals.emplace_back(std::move(item).value());
    }
  }(std::move(ag)));
  return vals;
}

TEST_F(TransformTest, TransformDeducesTypesCorrectlySyncFun) {
  auto makeStream = []() -> AsyncGenerator<int> { return ints(2); };
  using fv = std::vector<float>;
  using dv = std::vector<double>;

  {
    // Function returning value
    std::function<float(int)> fun = [](int) { return 1.f; };

    // Default deduced as r-value reference
    AsyncGenerator<float&&> s0 = transform(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s0)), (fv{1.f, 1.f, 1.f}));
    AsyncGenerator<float&&> s1 = transform<float&&>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s1)), (fv{1.f, 1.f, 1.f}));
    AsyncGenerator<float&> s2 = transform<float&>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s2)), (fv{1.f, 1.f, 1.f}));
    AsyncGenerator<float> s3 = transform<float>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s3)), (fv{1.f, 1.f, 1.f}));
  }

  {
    // Function returning reference

    std::function<float&(float&)> fun = [&](float& f) -> float& { return f; };

    // Default deduced as l-value reference
    AsyncGenerator<float&> s0 = transform(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s0)), (fv{0.f, 1.f, 2.f}));
    AsyncGenerator<float&&> s1 = transform<float&&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s1)), (fv{0.f, 1.f, 2.f}));
    AsyncGenerator<float&> s2 = transform<float&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s2)), (fv{0.f, 1.f, 2.f}));
    AsyncGenerator<float> s3 = transform<float>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s3)), (fv{0.f, 1.f, 2.f}));
  }

  // Conversion

  {
    // Function returning value
    std::function<float(int)> fun = [](int) { return 1.f; };

    AsyncGenerator<double&&> s1 = transform<double&&>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s1)), (dv{1., 1., 1.}));
    AsyncGenerator<double&> s2 = transform<double&>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s2)), (dv{1., 1., 1.}));
    AsyncGenerator<const double&> s3 =
        transform<const double&>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s3)), (dv{1., 1., 1.}));
    AsyncGenerator<double> s4 = transform<double>(makeStream(), fun);
    EXPECT_EQ(useStream(std::move(s4)), (dv{1., 1., 1.}));
  }

  {
    // Function returning reference
    std::function<float&(float&)> fun = [&](float& f) -> float& { return f; };

    AsyncGenerator<double&&> s1 = transform<double&&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s1)), (dv{0., 1., 2.}));
    AsyncGenerator<double&> s2 = transform<double&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s2)), (dv{0., 1., 2.}));
    AsyncGenerator<const double&> s3 =
        transform<const double&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s3)), (dv{0., 1., 2.}));
    AsyncGenerator<double&&> s4 = transform<double&&>(floatRefs(2), fun);
    EXPECT_EQ(useStream(std::move(s4)), (dv{0., 1., 2.}));
  }
}

#endif
