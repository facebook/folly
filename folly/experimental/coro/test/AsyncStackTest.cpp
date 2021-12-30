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

#include <folly/Portability.h>

#include <folly/experimental/coro/AsyncStack.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/tracing/AsyncStack.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

class AsyncStackTest : public testing::Test {};

TEST_F(AsyncStackTest, SimpleStackTrace) {
  blockingWait([&]() -> Task<void> {
    auto trace = co_await co_current_async_stack_trace;
    // [0] - current coroutine IP
    // [1] - BlockingWaitTask IP
    // [2] - blockingWait()
    // [3] - SimpleStackTrace_Test::TestBody()
    CHECK_EQ(4, trace.size());
    CHECK(trace[0] != 0);
    CHECK(trace[1] != 0);
    CHECK(trace[2] != 0);
    CHECK(trace[3] != 0);
  }());
}

TEST_F(AsyncStackTest, NestedStackTrace) {
  blockingWait([]() -> Task<void> { // Coroutine 1
    co_await []() -> Task<void> { // Coroutine 2
      co_await []() -> Task<void> { // Coroutine 3
        auto trace = co_await co_current_async_stack_trace;
        // [0] - Coroutine 3 IP
        // [1] - Coroutine 2 IP
        // [2] - Coroutine 1 IP
        // [3] - BlockingWaitTask
        // [4] - blockingWait()
        // [5] - NestedStackTrace_Test::TestBody()
        CHECK_EQ(6, trace.size());
        CHECK(trace[0] != 0);
        CHECK(trace[1] != 0);
        CHECK(trace[2] != 0);
        CHECK(trace[3] != 0);
        CHECK(trace[4] != 0);
        CHECK(trace[5] != 0);
      }();
    }();
  }());
}

TEST_F(AsyncStackTest, CollectAll) {
  blockingWait([]() -> Task<void> { // Coroutine 1
    folly::coro::Baton b;
    auto makeTask = [&]() -> Task<std::vector<std::uintptr_t>> {
      co_return co_await co_current_async_stack_trace;
    };

    auto [stack1, stack2] = co_await collectAll(makeTask(), makeTask());

    // Instruction pointers should be the same.
    CHECK(stack1 != stack2);

    // [0] - lambda coroutine
    // [1] - BarrierTask
    // [2] - collectAll
    // [3] - this coroutine
    // [4] - BlockingWaitTask
    // [5] - blockingWait()
    // [6] - CollectAll_Test::TestBody()
    CHECK_EQ(7, stack1.size());
    CHECK_EQ(7, stack2.size());

    CHECK_EQ(stack1[0], stack2[0]);
    CHECK_EQ(stack1[1], stack2[1]);
    CHECK_NE(stack1[2], stack2[2]); // Should be started from different
                                    // addresses in collectAll().
    CHECK_EQ(stack1[3], stack2[3]);
    CHECK_EQ(stack1[4], stack2[4]);
    CHECK_EQ(stack1[5], stack2[5]);
    CHECK_EQ(stack1[6], stack2[6]);

    auto afterStack = co_await co_current_async_stack_trace;
    CHECK_EQ(4, afterStack.size());
    CHECK_NE(afterStack[0], stack1[3]);
    CHECK_EQ(afterStack[1], stack1[4]);
    CHECK_EQ(afterStack[2], stack1[5]);
    CHECK_EQ(afterStack[3], stack1[6]);
  }());
}

#if defined(__linux__) && FOLLY_X64

struct stack_frame {
  stack_frame* nextFrame;
  void (*returnAddress)();
};

FOLLY_NOINLINE std::vector<std::uintptr_t> walk_stack() {
  auto& root = folly::getCurrentAsyncStackRoot();

  void* asyncRootReturnAddress = root.getReturnAddress();

  stack_frame* stackFrame = (stack_frame*)FOLLY_ASYNC_STACK_FRAME_POINTER();
  CHECK(stackFrame != nullptr);

  std::vector<std::uintptr_t> stack;

  while (stackFrame->nextFrame->returnAddress != asyncRootReturnAddress) {
    stack.push_back(
        reinterpret_cast<std::uintptr_t>(stackFrame->returnAddress));
    stackFrame = stackFrame->nextFrame;
  }

  CHECK(root.getTopFrame() != nullptr);
  for (auto* asyncFrame = root.getTopFrame(); asyncFrame != nullptr;
       asyncFrame = asyncFrame->getParentFrame()) {
    stack.push_back(
        reinterpret_cast<std::uintptr_t>(asyncFrame->getReturnAddress()));
  }

  return stack;
}

FOLLY_NOINLINE void normalFunction() {
  auto stack1 = walk_stack();
  // Stack should be:
  //  1) normalFunction()
  //  2) coro1()
  //  3) coro2()
  //  4) makeRefBlockingWaitTask()
  //  5) blockingWait()
  //  6) MixedStackWalk_Test::TestBody()
  // Note that some extra frames could be in-between these depending on inlining
  // of std library primitives (e.g. coroutine_handle).

  CHECK_LE(6, stack1.size());

  auto stack2 = walk_stack();

  CHECK_LE(6, stack2.size());

  // All except the topmost stack-frame should be the same.
  CHECK(std::equal(
      stack1.begin() + 1, stack1.end(), stack2.begin() + 1, stack2.end()));
}

folly::coro::Task<void> coro1() {
  normalFunction();
  co_return;
}

folly::coro::Task<void> coro2() {
  co_await coro1();
}

TEST_F(AsyncStackTest, MixedStackWalk) {
  folly::coro::blockingWait(coro2());
}

#endif // defined(__linux__) && FOLLY_X64

#endif // FOLLY_HAS_COROUTINES
