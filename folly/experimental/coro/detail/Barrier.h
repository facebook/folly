/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/tracing/AsyncStack.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

// A Barrier is a synchronisation building block that can be used to
// implement higher-level coroutine-based synchronisation primitives.
//
// It allows a single coroutine to wait until a counter reaches zero.
// The counter typically represents the amount of outstanding work.
// When a coroutine completes some work it should call arrive() which
// will return a continuation.
class Barrier {
 public:
  explicit Barrier(std::size_t initialCount = 0) noexcept
      : count_(initialCount) {}

  void add(std::size_t count = 1) noexcept {
    [[maybe_unused]] std::size_t oldCount =
        count_.fetch_add(count, std::memory_order_relaxed);
    // Check we didn't overflow the count.
    assert(SIZE_MAX - oldCount >= count);
  }

  // Query the number of remaining tasks that the barrier is waiting
  // for. This indicates the number of arrive() calls that must be
  // made before the Barrier will be released.
  //
  // Note that this should just be used as an approximate guide
  // for the number of outstanding tasks. This value may be out
  // of date immediately upon being returned.
  std::size_t remaining() const noexcept {
    return count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] coroutine_handle<> arrive(
      folly::AsyncStackFrame& currentFrame) noexcept {
    auto& stackRoot = *currentFrame.getStackRoot();
    folly::deactivateAsyncStackFrame(currentFrame);

    const std::size_t oldCount = count_.fetch_sub(1, std::memory_order_acq_rel);

    // Invalid to call arrive() if you haven't previously incremented the
    // counter using .add().
    assert(oldCount >= 1);

    if (oldCount == 1) {
      if (asyncFrame_ != nullptr) {
        folly::activateAsyncStackFrame(stackRoot, *asyncFrame_);
      }
      return std::exchange(continuation_, {});
    } else {
      return coro::noop_coroutine();
    }
  }

  [[nodiscard]] coroutine_handle<> arrive() noexcept {
    const std::size_t oldCount = count_.fetch_sub(1, std::memory_order_acq_rel);

    // Invalid to call arrive() if you haven't previously incremented the
    // counter using .add().
    assert(oldCount >= 1);

    if (oldCount == 1) {
      auto coro = std::exchange(continuation_, {});
      if (asyncFrame_ != nullptr) {
        folly::resumeCoroutineWithNewAsyncStackRoot(coro, *asyncFrame_);
        return coro::noop_coroutine();
      } else {
        return coro;
      }
    } else {
      return coro::noop_coroutine();
    }
  }

 private:
  class Awaiter {
   public:
    explicit Awaiter(Barrier& barrier) noexcept : barrier_(barrier) {}

    bool await_ready() { return false; }

    template <typename Promise>
    coroutine_handle<> await_suspend(
        coroutine_handle<Promise> continuation) noexcept {
      if constexpr (detail::promiseHasAsyncFrame_v<Promise>) {
        barrier_.setContinuation(
            continuation, &continuation.promise().getAsyncFrame());
        return barrier_.arrive(continuation.promise().getAsyncFrame());
      } else {
        barrier_.setContinuation(continuation, nullptr);
        return barrier_.arrive();
      }
    }

    void await_resume() noexcept {}

   private:
    friend Awaiter tag_invoke(
        cpo_t<co_withAsyncStack>, Awaiter&& awaiter) noexcept {
      return Awaiter{awaiter.barrier_};
    }

    Barrier& barrier_;
  };

 public:
  auto arriveAndWait() noexcept { return Awaiter{*this}; }

  void setContinuation(
      coroutine_handle<> continuation,
      folly::AsyncStackFrame* parentFrame) noexcept {
    assert(!continuation_);
    continuation_ = continuation;
    asyncFrame_ = parentFrame;
  }

 private:
  std::atomic<std::size_t> count_;
  coroutine_handle<> continuation_;
  folly::AsyncStackFrame* asyncFrame_ = nullptr;
};

} // namespace detail
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
