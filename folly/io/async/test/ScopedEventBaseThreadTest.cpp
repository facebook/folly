/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/io/async/ScopedEventBaseThread.h>

#include <chrono>
#include <folly/Baton.h>

#include <gtest/gtest.h>

using namespace std;
using namespace std::chrono;
using namespace folly;

class ScopedEventBaseThreadTest : public testing::Test {};

TEST_F(ScopedEventBaseThreadTest, example) {
  ScopedEventBaseThread sebt;

  Baton<> done;
  sebt.getEventBase()->runInEventBaseThread([&] { done.post(); });
  done.timed_wait(steady_clock::now() + milliseconds(100));
}

TEST_F(ScopedEventBaseThreadTest, start_stop) {
  ScopedEventBaseThread sebt(false);

  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(nullptr, sebt.getEventBase());
    sebt.start();
    EXPECT_NE(nullptr, sebt.getEventBase());

    Baton<> done;
    sebt.getEventBase()->runInEventBaseThread([&] { done.post(); });
    done.timed_wait(steady_clock::now() + milliseconds(100));

    EXPECT_NE(nullptr, sebt.getEventBase());
    sebt.stop();
    EXPECT_EQ(nullptr, sebt.getEventBase());
  }
}

TEST_F(ScopedEventBaseThreadTest, move) {
  auto sebt0 = ScopedEventBaseThread();
  auto sebt1 = std::move(sebt0);
  auto sebt2 = std::move(sebt1);

  EXPECT_EQ(nullptr, sebt0.getEventBase());
  EXPECT_EQ(nullptr, sebt1.getEventBase());
  EXPECT_NE(nullptr, sebt2.getEventBase());

  Baton<> done;
  sebt2.getEventBase()->runInEventBaseThread([&] { done.post(); });
  done.timed_wait(steady_clock::now() + milliseconds(100));
}

TEST_F(ScopedEventBaseThreadTest, self_move) {
  ScopedEventBaseThread sebt0;
  auto sebt = std::move(sebt0);

  EXPECT_NE(nullptr, sebt.getEventBase());

  Baton<> done;
  sebt.getEventBase()->runInEventBaseThread([&] { done.post(); });
  done.timed_wait(steady_clock::now() + milliseconds(100));
}

TEST_F(ScopedEventBaseThreadTest, manager) {
  EventBaseManager ebm;
  ScopedEventBaseThread sebt(&ebm);
  auto sebt_eb = sebt.getEventBase();
  auto ebm_eb = (EventBase*)nullptr;
  sebt_eb->runInEventBaseThreadAndWait([&] {
      ebm_eb = ebm.getEventBase();
  });
  EXPECT_EQ(uintptr_t(sebt_eb), uintptr_t(ebm_eb));
}
