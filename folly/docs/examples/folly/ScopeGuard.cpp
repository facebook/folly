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

#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>

TEST(scopeGuard, demo) {
  int x = 0;
  {
    auto guard = folly::makeGuard([&]() { x += 1; });
    EXPECT_EQ(x, 0);
  }
  EXPECT_EQ(x, 1);
}

TEST(scopeExit, demo) {
  int y = 0;
  {
    SCOPE_EXIT { y = 1234; };
    EXPECT_EQ(y, 0);
  }
  EXPECT_EQ(y, 1234);
}
