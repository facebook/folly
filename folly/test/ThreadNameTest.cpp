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

#include <thread>
#include <folly/Baton.h>
#include <folly/ThreadName.h>
#include <gtest/gtest.h>

using namespace std;
using namespace folly;

TEST(ThreadName, setThreadName_self) {
  thread th([] {
      EXPECT_TRUE(setThreadName("rockin-thread"));
  });
  SCOPE_EXIT { th.join(); };
}

TEST(ThreadName, setThreadName_other_pthread) {
  Baton<> handle_set;
  Baton<> let_thread_end;
  pthread_t handle;
  thread th([&] {
      handle = pthread_self();
      handle_set.post();
      let_thread_end.wait();
  });
  SCOPE_EXIT { th.join(); };
  handle_set.wait();
  SCOPE_EXIT { let_thread_end.post(); };
  EXPECT_TRUE(setThreadName(handle, "rockin-thread"));
}

TEST(ThreadName, setThreadName_other_native) {
  Baton<> let_thread_end;
  thread th([&] {
      let_thread_end.wait();
  });
  SCOPE_EXIT { th.join(); };
  SCOPE_EXIT { let_thread_end.post(); };
  EXPECT_TRUE(setThreadName(th.native_handle(), "rockin-thread"));
}
