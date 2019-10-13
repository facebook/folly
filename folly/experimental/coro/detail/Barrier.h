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

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <experimental/coroutine>
#include <utility>

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

  [[nodiscard]] std::experimental::coroutine_handle<> arrive() noexcept {
    const std::size_t oldCount = count_.fetch_sub(1, std::memory_order_acq_rel);

    // Invalid to call arrive() if you haven't previously incremented the
    // counter using .add().
    assert(oldCount >= 1);

    if (oldCount == 1) {
      return std::exchange(continuation_, {});
    } else {
      return std::experimental::noop_coroutine();
    }
  }

  auto arriveAndWait() noexcept {
    class Awaiter {
     public:
      explicit Awaiter(Barrier& barrier) noexcept : barrier_(barrier) {}
      bool await_ready() {
        return false;
      }
      std::experimental::coroutine_handle<> await_suspend(
          std::experimental::coroutine_handle<> continuation) noexcept {
        barrier_.setContinuation(continuation);
        return barrier_.arrive();
      }
      void await_resume() noexcept {}

     private:
      Barrier& barrier_;
    };

    return Awaiter{*this};
  }

  void setContinuation(
      std::experimental::coroutine_handle<> continuation) noexcept {
    assert(!continuation_);
    continuation_ = continuation;
  }

 private:
  std::atomic<std::size_t> count_;
  std::experimental::coroutine_handle<> continuation_;
};

} // namespace detail
} // namespace coro
} // namespace folly
