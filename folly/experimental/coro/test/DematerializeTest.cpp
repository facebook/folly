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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Dematerialize.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

using namespace folly::coro;

class DematerializeTest : public testing::Test {};

TEST_F(DematerializeTest, SimpleStream) {
  struct MyError : std::exception {};

  const int seenEndOfStream = 100;
  const int seenError = 200;

  std::vector<int> lastSeen(3, -1);
  std::vector<int> expectedSeen = {
      {seenEndOfStream, seenEndOfStream, seenError}};

  auto selectStream = [](int index) -> AsyncGenerator<int> {
    auto ints = [](int n) -> AsyncGenerator<int> {
      for (int i = 0; i <= n; ++i) {
        co_await co_reschedule_on_current_executor;
        co_yield i;
      }
    };

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
    auto generator = dematerialize(materialize(selectStream(index)));
    try {
      while (auto item = co_await generator.next()) {
        auto value = *item;
        CHECK(index >= 0 && index <= 2);
        CHECK_EQ(lastSeen[index] + 1, value);
        lastSeen[index] = value;
      }

      // None
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

TEST_F(DematerializeTest, GeneratorOfRValueReference) {
  auto makeGenerator =
      []() -> folly::coro::AsyncGenerator<std::unique_ptr<int>&&> {
    co_yield std::make_unique<int>(10);

    auto ptr = std::make_unique<int>(20);
    co_yield std::move(ptr);
    CHECK(ptr == nullptr);
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto gen = dematerialize(materialize(makeGenerator()));

    auto result = co_await gen.next();
    CHECK_EQ(10, **result);
    // Don't move it to a local var.

    result = co_await gen.next();
    CHECK_EQ(20, **result);
    auto ptr = *result; // Move it to a local var.

    result = co_await gen.next();
    CHECK(!result);
  }());
}

struct MoveOnly {
  explicit MoveOnly(int value) : value_(value) {}
  MoveOnly(MoveOnly&& other) noexcept
      : value_(std::exchange(other.value_, -1)) {}
  ~MoveOnly() {}
  MoveOnly& operator=(MoveOnly&&) = delete;
  int value() const {
    return value_;
  }

 private:
  int value_;
};

TEST_F(DematerializeTest, GeneratorOfMoveOnlyType) {
  auto makeGenerator = []() -> folly::coro::AsyncGenerator<MoveOnly> {
    MoveOnly rvalue(1);
    co_yield std::move(rvalue);
    CHECK_EQ(-1, rvalue.value());

    co_yield MoveOnly(2);
  };

  folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
    auto gen = dematerialize(materialize(makeGenerator()));
    auto result = co_await gen.next();

    // NOTE: It's an error to dereference using '*it' as this returns a copy
    // of the iterator's reference type, which in this case is 'MoveOnly'.
    CHECK_EQ(1, result->value());

    result = co_await gen.next();
    CHECK_EQ(2, result->value());

    result = co_await gen.next();
    CHECK(!result);
  }());
}

#endif
