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

#include <folly/container/Array.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(array, fromlist) {
  // Two different ways to make an array of squares
  auto a = make_array<int>(0, 1, 4, 9, 16);

  auto squareFn = [](int i) { return i * i; };
  auto b = make_array_with<5>(squareFn);

  EXPECT_EQ(a.size(), 5);
  EXPECT_EQ(a, b);
}
