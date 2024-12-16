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

#include <folly/fibers/Semaphore.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/detail/Sleeper.h>

using namespace folly::fibers;

TEST(SemaphoreTest, MessagePassing) {
  int data = 0;
  Semaphore sem{0};

  // Provides no memory ordering: just used to reproduce conditions for a
  // bug caught by TSAN in an earlier version of the implementation.
  folly::relaxed_atomic<bool> signalled{false};

  std::thread t{[&]() {
    folly::detail::Sleeper sleeper;
    while (!signalled) {
      sleeper.wait();
    }
    sem.wait();
    EXPECT_NE(0, data);
  }};

  data = 1;
  sem.signal();
  signalled = true;

  t.join();
}
