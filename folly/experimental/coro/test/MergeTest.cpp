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

#include <folly/CancellationToken.h>
#include <folly/ScopeGuard.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Merge.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

class MergeTest : public testing::Test {};

TEST_F(MergeTest, SimpleMerge) {
  blockingWait([]() -> Task<void> {
    auto generator = merge(
        co_await co_current_executor,
        []() -> AsyncGenerator<AsyncGenerator<int>> {
          auto makeGenerator = [](int start, int count) -> AsyncGenerator<int> {
            for (int i = start; i < start + count; ++i) {
              co_yield i;
              co_await co_reschedule_on_current_executor;
            }
          };

          co_yield makeGenerator(0, 3);
          co_yield makeGenerator(3, 2);
        }());

    const std::array<int, 5> expectedValues = {{0, 3, 1, 4, 2}};

    auto item = co_await generator.next();
    for (int expectedValue : expectedValues) {
      CHECK(!!item);
      CHECK_EQ(expectedValue, *item);
      item = co_await generator.next();
    }
    CHECK(!item);
  }());
}

TEST_F(MergeTest, TruncateStream) {
  blockingWait([]() -> Task<void> {
    int started = 0;
    int completed = 0;
    {
      auto generator = merge(
          co_await co_current_executor,
          co_invoke([&]() -> AsyncGenerator<AsyncGenerator<int>> {
            auto makeGenerator = [&]() -> AsyncGenerator<int> {
              ++started;
              SCOPE_EXIT {
                ++completed;
              };
              co_yield 1;
              co_await co_reschedule_on_current_executor;
              co_yield 2;
            };

            co_yield co_invoke(makeGenerator);
            co_yield co_invoke(makeGenerator);
            co_yield co_invoke(makeGenerator);
          }));

      auto item = co_await generator.next();
      CHECK_EQ(1, *item);
      item = co_await generator.next();
      CHECK_EQ(1, *item);
      CHECK_EQ(3, started);
      // Truncate the stream after consuming only 2 of the 6 values it
      // would have produced.
    }

    // Spin the executor until the generators finish responding to cancellation.
    for (int i = 0; completed != started && i < 10; ++i) {
      co_await co_reschedule_on_current_executor;
    }

    CHECK_EQ(3, completed);
  }());
}

TEST_F(MergeTest, SequencesOfRValueReferences) {
  blockingWait([]() -> Task<void> {
    auto makeStreamOfStreams =
        []() -> AsyncGenerator<AsyncGenerator<std::vector<int>&&>> {
      auto makeStreamOfVectors = []() -> AsyncGenerator<std::vector<int>&&> {
        co_yield std::vector{1, 2, 3};
        co_await co_reschedule_on_current_executor;
        co_yield std::vector{2, 4, 6};
      };

      co_yield makeStreamOfVectors();
      co_yield makeStreamOfVectors();
    };

    auto gen = merge(co_await co_current_executor, makeStreamOfStreams());
    int resultCount = 0;
    while (auto item = co_await gen.next()) {
      ++resultCount;
      std::vector<int>&& v = *item;
      CHECK_EQ(3, v.size());
    }
    CHECK_EQ(4, resultCount);
  }());
}

TEST_F(MergeTest, SequencesOfLValueReferences) {
  blockingWait([]() -> Task<void> {
    auto makeStreamOfStreams =
        []() -> AsyncGenerator<AsyncGenerator<std::vector<int>&>> {
      auto makeStreamOfVectors = []() -> AsyncGenerator<std::vector<int>&> {
        std::vector<int> v{1, 2, 3};
        co_yield v;
        CHECK_EQ(4, v.size());
        co_await co_reschedule_on_current_executor;
        v.push_back(v.back());
        co_yield v;
      };

      co_yield makeStreamOfVectors();
      co_yield makeStreamOfVectors();
    };

    auto gen = merge(co_await co_current_executor, makeStreamOfStreams());
    int resultCount = 0;
    while (auto item = co_await gen.next()) {
      ++resultCount;
      std::vector<int>& v = *item;
      if (v.size() == 3) {
        CHECK_EQ(1, v[0]);
        CHECK_EQ(2, v[1]);
        CHECK_EQ(3, v[2]);
        v.push_back(7);
      } else {
        CHECK_EQ(5, v.size());
        CHECK_EQ(1, v[0]);
        CHECK_EQ(2, v[1]);
        CHECK_EQ(3, v[2]);
        CHECK_EQ(7, v[3]);
        CHECK_EQ(7, v[4]);
      }
    }
    CHECK_EQ(4, resultCount);
  }());
}

template <typename Ref, typename Value = folly::remove_cvref_t<Ref>>
folly::coro::AsyncGenerator<Ref, Value> neverStream() {
  folly::coro::Baton baton;
  folly::CancellationCallback cb{
      co_await folly::coro::co_current_cancellation_token,
      [&] { baton.post(); }};
  co_await baton;
}

TEST_F(MergeTest, CancellationTokenPropagatesToOuterFromConsumer) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    bool suspended = false;
    bool done = false;
    co_await folly::coro::collectAll(
        folly::coro::co_withCancellation(
            cancelSource.getToken(),
            [&]() -> folly::coro::Task<void> {
              auto stream = merge(
                  co_await co_current_executor,
                  neverStream<AsyncGenerator<int>>());
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

TEST_F(MergeTest, CancellationTokenPropagatesToInnerFromConsumer) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    folly::CancellationSource cancelSource;
    bool suspended = false;
    bool done = false;
    auto makeStreamOfStreams = []() -> AsyncGenerator<AsyncGenerator<int>> {
      co_yield neverStream<int>();
      co_yield neverStream<int>();
    };

    co_await folly::coro::collectAll(
        folly::coro::co_withCancellation(
            cancelSource.getToken(),
            [&]() -> folly::coro::Task<void> {
              auto stream =
                  merge(co_await co_current_executor, makeStreamOfStreams());
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

#endif
