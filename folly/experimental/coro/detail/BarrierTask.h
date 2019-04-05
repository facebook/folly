/*
 * Copyright 2019-present Facebook, Inc.
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
#pragma once

#include <folly/experimental/coro/detail/Barrier.h>

#include <cassert>
#include <experimental/coroutine>
#include <utility>

namespace folly {
namespace coro {
namespace detail {

class BarrierTask;

class BarrierTaskPromise {};

class BarrierTask {
 public:
  class promise_type {
    struct FinalAwaiter {
      bool await_ready() noexcept {
        return false;
      }
      std::experimental::coroutine_handle<> await_suspend(
          std::experimental::coroutine_handle<promise_type> h) noexcept {
        auto& promise = h.promise();
        assert(promise.barrier_ != nullptr);
        return promise.barrier_->arrive();
      }
      void await_resume() noexcept {}
    };

   public:
    BarrierTask get_return_object() noexcept {
      return BarrierTask{
          std::experimental::coroutine_handle<promise_type>::from_promise(
              *this)};
    }

    std::experimental::suspend_always initial_suspend() noexcept {
      return {};
    }

    FinalAwaiter final_suspend() noexcept {
      return {};
    }

    void return_void() noexcept {}

    [[noreturn]] void unhandled_exception() noexcept {
      std::terminate();
    }

    void setBarrier(Barrier* barrier) noexcept {
      assert(barrier_ == nullptr);
      barrier_ = barrier;
    }

   private:
    Barrier* barrier_ = nullptr;
  };

 private:
  using handle_t = std::experimental::coroutine_handle<promise_type>;

  explicit BarrierTask(handle_t coro) noexcept : coro_(coro) {}

 public:
  BarrierTask(BarrierTask&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~BarrierTask() {
    if (coro_) {
      coro_.destroy();
    }
  }

  BarrierTask& operator=(BarrierTask other) noexcept {
    swap(other);
    return *this;
  }

  void swap(BarrierTask& b) noexcept {
    std::swap(coro_, b.coro_);
  }

  void start(Barrier* barrier) noexcept {
    assert(coro_);
    coro_.promise().setBarrier(barrier);
    coro_.resume();
  }

  auto startAndWaitForBarrier(Barrier* barrier) noexcept {
    class awaiter {
     public:
      explicit awaiter(Barrier* barrier, handle_t coro) noexcept
          : barrier_(barrier), coro_(coro) {}
      bool await_ready() noexcept {
        return false;
      }
      std::experimental::coroutine_handle<> await_suspend(
          std::experimental::coroutine_handle<> continuation) noexcept {
        coro_.promise().setBarrier(barrier_);
        barrier_->setContinuation(continuation);
        return coro_;
      }
      void await_resume() noexcept {}

     private:
      Barrier* barrier_;
      handle_t coro_;
    };

    assert(coro_);
    return awaiter{barrier, coro_};
  }

 private:
  handle_t coro_;
};

} // namespace detail
} // namespace coro
} // namespace folly
