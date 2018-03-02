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

TEST(Optional, CoroutineSuccess) {
  auto r0 = []() -> Optional<int> {
    auto x = co_await f1();
    EXPECT_EQ(7, x);
    auto y = co_await f2(x);
    EXPECT_EQ(2.0 * 7, y);
    auto z = co_await f3(x, y);
    EXPECT_EQ((int)(2.0 * 7 + 7), *z);
    co_return* z;
  }();
  EXPECT_TRUE(r0.hasValue());
  EXPECT_EQ(21, *r0);
}

#endif
