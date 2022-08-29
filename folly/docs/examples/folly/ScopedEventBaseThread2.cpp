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
#include <string>

#include <folly/Optional.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/ThreadName.h>

using folly::Baton;
using folly::EventBase;
using folly::EventBaseManager;
using folly::Optional;
using folly::ScopedEventBaseThread;
using folly::StringPiece;
using std::chrono::seconds;

TEST(ScopedEventBaseThread, demo1) {
  constexpr StringPiece kThreadName{"my_thread"};
  ScopedEventBaseThread t2(kThreadName);
  Optional<std::string> createdThreadName;
  Baton<> done;

  t2.getEventBase()->runInEventBaseThread([&] {
    createdThreadName = folly::getCurrentThreadName();
    done.post();
  });

  ASSERT_TRUE(done.try_wait_for(seconds(1)));
  if (createdThreadName) {
    ASSERT_EQ(kThreadName.toString(), createdThreadName.value());
  }
}

TEST(ScopedEventBaseThread, demo2) {
  EventBaseManager ebm;
  ScopedEventBaseThread t3(&ebm);

  auto t3_eb = t3.getEventBase();
  auto ebm_eb = static_cast<EventBase*>(nullptr);
  t3_eb->runInEventBaseThreadAndWait([&] { ebm_eb = ebm.getEventBase(); });
  EXPECT_EQ(uintptr_t(t3_eb), uintptr_t(ebm_eb));
}
