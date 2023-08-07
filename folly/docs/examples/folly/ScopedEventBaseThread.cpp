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

#include <folly/io/async/ScopedEventBaseThread.h>

#include <chrono>

#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using folly::Baton;
using folly::ScopedEventBaseThread;
using std::chrono::seconds;

TEST(ScopedEventBaseThread, demo1) {
  ScopedEventBaseThread t1;
  Baton<> done;

  t1.getEventBase()->runInEventBaseThread([&] { done.post(); });
  ASSERT_TRUE(done.try_wait_for(seconds(1)));
}
