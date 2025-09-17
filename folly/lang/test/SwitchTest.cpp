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

#include <folly/lang/Switch.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace folly {

class SwitchTest : public ::testing::Test {};

namespace {
enum class E {
  ZERO = 0,
  ONE = 1,
  TWO = 2,
};
} // namespace

TEST_F(SwitchTest, Exhaustive) {
  auto lambda = [](E e) -> std::int64_t {
    FOLLY_EXHAUSTIVE_SWITCH({
      switch (e) {
        case E::ZERO: {
          return 0;
        }
        case E::ONE: {
          return 1;
        }
        case E::TWO: {
          return 2;
        }
        default: {
          return -1;
        }
      }
    });
  };

  EXPECT_EQ(lambda(E::ZERO), 0);
  EXPECT_EQ(lambda(E::ONE), 1);
  EXPECT_EQ(lambda(E::TWO), 2);
  EXPECT_EQ(lambda(static_cast<E>(3)), -1);
}

TEST_F(SwitchTest, NonExhaustive) {
  auto lambda = [](E e) -> std::int64_t {
    FOLLY_NON_EXHAUSTIVE_SWITCH({
      switch (e) {
        case E::ZERO:
          return 0;
        case E::ONE:
          return 1;
        default:
          return -1;
      }
    });
  };

  EXPECT_EQ(lambda(E::ZERO), 0);
  EXPECT_EQ(lambda(E::ONE), 1);
  EXPECT_EQ(lambda(static_cast<E>(2)), -1);
}

} // namespace folly
