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

#include <atomic>
#include <thread>

#include <folly/futures/HeapTimekeeper.h>
#include <folly/futures/test/TimekeeperTestLib.h>

namespace folly {

INSTANTIATE_TYPED_TEST_SUITE_P(
    HeapTimekeeperTest, TimekeeperTest, HeapTimekeeper);

namespace {

struct WrapperCalls {
  std::atomic<bool> entered{false};
  std::atomic<bool> returned{false};
  std::atomic<std::thread::id> threadId{std::thread::id{}};
};

class WrappedHeapTimekeeper : public HeapTimekeeper {
 public:
  explicit WrappedHeapTimekeeper(WrapperCalls& calls)
      : HeapTimekeeper([&calls](FunctionRef<void()> runWorker) {
          calls.threadId.store(std::this_thread::get_id());
          calls.entered.store(true);
          runWorker();
          calls.returned.store(true);
        }) {}
};

} // namespace

TEST(HeapTimekeeperTest, WorkerRunsInsideSuppliedWrapper) {
  WrapperCalls calls;
  {
    WrappedHeapTimekeeper tk(calls);
    ASSERT_TRUE(
        tk.after(std::chrono::milliseconds{1}).wait(std::chrono::seconds{60}))
        << "timeout never fired, so the worker loop did not run";
    // The timeout fired from the worker loop, so the wrapper must have been
    // entered on the worker thread and must not have returned yet.
    EXPECT_TRUE(calls.entered.load());
    EXPECT_NE(calls.threadId.load(), std::this_thread::get_id());
    EXPECT_FALSE(calls.returned.load());
  }
  // The destructor joins the worker thread, so the wrapper has returned only
  // after the whole loop completed.
  EXPECT_TRUE(calls.returned.load());
}

TEST(TimekeeperSingletonTest, ExpectedType) {
  // This is just to check that the un-mocked default timekeeper singleton
  // Implementation is covered by some instantiation of the test suite. If the
  // default implementation is changed this test should be moved accordingly.
  ASSERT_TRUE(
      dynamic_cast<HeapTimekeeper*>(detail::getTimekeeperSingleton().get()) !=
      nullptr);
}

} // namespace folly
