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

#include <folly/synchronization/AtomicRef.h>

#include <folly/portability/GTest.h>

class AtomicRefTest : public testing::Test {};

TEST_F(AtomicRefTest, integer) {
  {
    long value = 17;
    auto ref = folly::make_atomic_ref(value);
    EXPECT_EQ(17, ref.load(std::memory_order_relaxed));
    ref.store(55, std::memory_order_relaxed);
    EXPECT_EQ(55, ref.load(std::memory_order_relaxed));
  }

  {
    long value = 17;
    auto ref = folly::make_atomic_ref(value);
    auto prev = ref.fetch_add(4, std::memory_order_relaxed);
    EXPECT_EQ(17, prev);
    EXPECT_EQ(21, value);
  }

  {
    long value = 17;
    auto ref = folly::make_atomic_ref(value);
    auto prev = ref.fetch_sub(4, std::memory_order_relaxed);
    EXPECT_EQ(17, prev);
    EXPECT_EQ(13, value);
  }
}
