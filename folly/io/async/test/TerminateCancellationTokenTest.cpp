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

#include <folly/io/async/TerminateCancellationToken.h>

#include <chrono>

#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;
using namespace std::chrono;

TEST(TerminateCancellationTokenTest, addCallbacks) {
  bool called1 = false, called2 = false;
  Baton<> done1, done2;
  auto cb1 = CancellationCallback(getTerminateCancellationToken(), [&]() {
    called1 = true;
    done1.post();
  });
  auto cb2 = CancellationCallback(getTerminateCancellationToken(), [&]() {
    called2 = true;
    done2.post();
  });
  kill(getpid(), SIGTERM);
  EXPECT_TRUE(done1.try_wait_for(seconds(2)));
  EXPECT_TRUE(called1);
  EXPECT_TRUE(done2.try_wait_for(seconds(2)));
  EXPECT_TRUE(called2);
}
