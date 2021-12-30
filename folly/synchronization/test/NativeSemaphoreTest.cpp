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

#include <folly/synchronization/NativeSemaphore.h>

#include <folly/portability/GTest.h>

using folly::NativeSemaphore;

class NativeSemaphoreTest : public testing::Test {};

TEST_F(NativeSemaphoreTest, empty) {
  NativeSemaphore sem;
  EXPECT_FALSE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait());

  sem.post();
  EXPECT_TRUE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait());

  sem.post();
  sem.wait();
  EXPECT_FALSE(sem.try_wait());
}
