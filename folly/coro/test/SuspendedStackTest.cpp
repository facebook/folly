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

#include <unordered_set>
#include <folly/coro/AsyncStack.h>
#include <folly/coro/Baton.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Mutex.h>
#include <folly/coro/Task.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

constexpr size_t coroDepth = 5;

#if FOLLY_HAS_COROUTINES

template <size_t Depth, typename Awaitable>
FOLLY_NOINLINE folly::coro::Task<void> waitOn(Awaitable&& aw) {
  if constexpr (Depth == 0) {
    co_await std::forward<Awaitable&&>(aw);
  } else {
    co_await waitOn<Depth - 1>(std::forward<Awaitable&&>(aw));
  }
  co_return;
}

static void expectSuspendedFrames(size_t numFrames, size_t stackDepth = 0) {
  struct StackAggregator {
    std::vector<std::vector<std::uintptr_t>> stacks;

    void operator()(folly::AsyncStackFrame* frame) {
      stacks.push_back(walkStack(frame));
    }

    std::vector<std::uintptr_t> walkStack(folly::AsyncStackFrame* topFrame) {
      std::array<std::uintptr_t, 128> frames;
      auto numFrames = folly::getAsyncStackTraceFromInitialFrame(
          topFrame, frames.data(), 128);
      return std::vector<std::uintptr_t>(
          frames.data(), frames.data() + numFrames);
    }

  } agg;

  folly::sweepSuspendedLeafFrames(agg);
  if constexpr (!folly::kIsDebug) {
    // Stack tracing doesn't work in non-debug builds
    CHECK_EQ(0, agg.stacks.size());
  } else {
    CHECK_EQ(numFrames, agg.stacks.size());

    for (const auto& stack : agg.stacks) {
      CHECK_EQ(stack.size(), stackDepth);
    }

    if (folly::symbolizer::Symbolizer::isAvailable()) {
      auto stacks = folly::symbolizer::getSuspendedStackTraces();
      CHECK_EQ(numFrames, stacks.size());
    }
  }
}

CO_TEST(SuspendedStacksTest, testBaton) {
  auto currentDepth =
      (co_await folly::coro::co_current_async_stack_trace).size();
  auto* ex = co_await folly::coro::co_current_executor;

  expectSuspendedFrames(0);

  folly::coro::Baton b;
  waitOn<coroDepth>(b).scheduleOn(ex).start();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(1, currentDepth + coroDepth);

  waitOn<coroDepth>(b).scheduleOn(ex).start();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(2, currentDepth + coroDepth);

  b.post();
  co_await folly::coro::co_reschedule_on_current_executor;

  expectSuspendedFrames(0);
}

CO_TEST(SuspendedStacksTest, testFibersBaton) {
  auto currentDepth =
      (co_await folly::coro::co_current_async_stack_trace).size();
  auto* ex = co_await folly::coro::co_current_executor;

  expectSuspendedFrames(0);

  folly::fibers::Baton b;
  waitOn<coroDepth>(b).scheduleOn(ex).start();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(1, currentDepth + coroDepth);

  // Only one fiber/coro can wait on a fibers::Baton

  b.post();
  co_await folly::coro::co_reschedule_on_current_executor;

  expectSuspendedFrames(0);
}

CO_TEST(SuspendedStacksTest, testLock) {
  auto currentDepth =
      (co_await folly::coro::co_current_async_stack_trace).size();

  folly::coro::Mutex m;
  co_await m.co_lock();

  expectSuspendedFrames(0);

  auto aw = m.co_lock();
  waitOn<coroDepth>(std::move(aw))
      .scheduleOn(co_await folly::coro::co_current_executor)
      .start();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(1, currentDepth + coroDepth);

  m.unlock();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(0);
}

CO_TEST(SuspendedStacksTest, testFuture) {
  auto currentDepth =
      (co_await folly::coro::co_current_async_stack_trace).size();
  auto [promise, future] = folly::makePromiseContract<int>();

  waitOn<coroDepth>(std::move(future))
      .scheduleOn(co_await folly::coro::co_current_executor)
      .start();
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(1, currentDepth + coroDepth);

  promise.setValue(1);
  co_await folly::coro::co_reschedule_on_current_executor;
  expectSuspendedFrames(0);
}

#endif
