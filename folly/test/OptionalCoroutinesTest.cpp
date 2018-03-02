/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES
using folly::Optional;

Optional<int> f1() {
  return 7;
}
Optional<double> f2(int x) {
  return 2.0 * x;
}

// move-only type
Optional<std::unique_ptr<int>> f3(int x, double y) {
  return std::make_unique<int>((int)(x + y));
}

Optional<int> f4(int, double) {
  return folly::none;
}

TEST(Optional, CoroutineFailure) {
  auto r1 = []() -> Optional<int> {
    auto x = co_await f1();
    auto y = co_await f2(x);
    auto z = co_await f4(x, y);
    ADD_FAILURE();
    co_return z;
  }();
  EXPECT_TRUE(!r1.hasValue());
}

Optional<int> throws() {
  throw 42;
}

TEST(Optional, CoroutineException) {
  try {
    auto r2 = []() -> Optional<int> {
      auto x = co_await throws();
      ADD_FAILURE();
      co_return x;
    }();
    (void)r2;
    ADD_FAILURE();
  } catch (/* nolint */ int i) {
    EXPECT_EQ(42, i);
  } catch (...) {
    ADD_FAILURE();
  }
}

// this test makes sure that the coroutine is destroyed properly
TEST(Optional, CoroutineCleanedUp) {
  int count_dest = 0;
  auto r = [&]() -> Optional<int> {
    SCOPE_EXIT {
      ++count_dest;
    };
    auto x = co_await folly::Optional<int>();
    ADD_FAILURE() << "Should not be resuming";
    co_return x;
  }();
  EXPECT_FALSE(r.hasValue());
  EXPECT_EQ(1, count_dest);
}

folly::Optional<int> f5(int x) {
  return folly::make_optional(x * 3);
}

folly::Optional<int> f6(int n) {
  auto x = co_await f5(n);
  co_return x + 9;
}

TEST(Optional, CoroutineDanglingReference) {
  EXPECT_EQ(*f6(1), 12);
}

#endif
