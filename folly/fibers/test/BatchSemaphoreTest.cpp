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

#include <folly/fibers/BatchSemaphore.h>
#include <folly/portability/GTest.h>

TEST(BatchSemaphoreTest, WaitSignalSynchronization) {
  folly::fibers::BatchSemaphore sem{0};

  int64_t data = 0;
  folly::relaxed_atomic_bool signalled = false;

  std::jthread t{[&]() {
    while (!signalled) {
      std::this_thread::yield();
    }

    sem.wait(1);
    EXPECT_NE(data, 0);
  }};

  data = 1;
  sem.signal(1);
  signalled = true;
}
