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

#include <thread>
#include <folly/Synchronized.h>
#include <folly/portability/GTest.h>

TEST(Synchronized, demo) {
  folly::Synchronized<int> si(123);
  std::thread t;
  int val = 0;

  { // Best practice: open a new scope before acquiring lock
    auto wlock = si.wlock();

    t = std::thread([&]() {
      // Acquire read lock, then read the value
      auto rlock = si.rlock();
      val = *rlock;
    });

    *wlock = 456;
  } // wlock is released here, when it goes out of scope

  t.join();

  EXPECT_EQ(val, 456);
}
