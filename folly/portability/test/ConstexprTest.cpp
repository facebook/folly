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

#include <folly/portability/Constexpr.h>

#include <folly/portability/GTest.h>

namespace {

class ConstexprTest : public testing::Test {};
}

TEST_F(ConstexprTest, constexpr_strlen_cstr) {
  constexpr auto v = "hello";
  constexpr auto a = folly::constexpr_strlen(v);
  EXPECT_EQ(5, a);
  EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
}

TEST_F(ConstexprTest, constexpr_strlen_ints) {
  constexpr int v[] = {5, 3, 4, 0, 7};
  constexpr auto a = folly::constexpr_strlen(v);
  EXPECT_EQ(3, a);
  EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
}
