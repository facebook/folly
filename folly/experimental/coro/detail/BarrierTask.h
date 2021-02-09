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
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/Malloc.h>

#include <cassert>
#include <utility>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

class BarrierTask {
 public:
  class promise_type {
    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }

      coroutine_handle<> await_suspend(
          coroutine_handle<promise_type> h) noexcept {
        auto& promise = h.promise();
        assert(promise.barrier_ != nullptr);
        return promise.barrier_->arrive(promise.asyncFrame_);
      }

      void await_resume() noexcept {}
    };

   public:
    static void* operator new(std::size_t size) {
      return ::folly_coro_async_malloc(size);
    }

    static void operator delete(void* ptr, std::size_t size) {
      ::folly_coro_async_free(ptr, size);
    }

    BarrierTask get_return_object() noexcept {
      return BarrierTask{coroutine_handle<promise_type>::from_promise(*this)};
    }

    suspend_always initial_suspend() noexcept { return {}; }

    FinalAwaiter final_suspend() noexcept { return {}; }

    template <typename Awaitable>
    auto await_transform(Awaitable&& awaitable) {
      return folly::coro::co_withAsyncStack(
          static_cast<Awaitable&&>(awaitable));
    }

    void return_void() noexcept {}

    [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }

    void setBarrier(Barrier* barrier) noexcept {
      assert(barrier_ == nullptr);
      barrier_ = barrier;
    }

    folly::AsyncStackFrame& getAsyncFrame() noexcept { return asyncFrame_; }

   private:
    folly::AsyncStackFrame asyncFrame_;
    Barrier* barrier_ = nullptr;
  };

 private:
  using handle_t = coroutine_handle<promise_type>;

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

  void swap(BarrierTask& b) noexcept { std::swap(coro_, b.coro_); }

  FOLLY_NOINLINE void start(Barrier* barrier) noexcept {
    start(barrier, folly::getDetachedRootAsyncStackFrame());
  }

  FOLLY_NOINLINE void start(
      Barrier* barrier, folly::AsyncStackFrame& parentFrame) noexcept {
    assert(coro_);
    auto& calleeFrame = coro_.promise().getAsyncFrame();
    calleeFrame.setParentFrame(parentFrame);
    calleeFrame.setReturnAddress();
    coro_.promise().setBarrier(barrier);

    folly::resumeCoroutineWithNewAsyncStackRoot(coro_);
  }

 private:
  handle_t coro_;
};

class DetachedBarrierTask {
 public:
  class promise_type {
   public:
    promise_type() noexcept {
      asyncFrame_.setParentFrame(folly::getDetachedRootAsyncStackFrame());
    }

    DetachedBarrierTask get_return_object() noexcept {
      return DetachedBarrierTask{
          coroutine_handle<promise_type>::from_promise(*this)};
    }

    suspend_always initial_suspend() noexcept { return {}; }

    auto final_suspend() noexcept {
      struct awaiter {
        bool await_ready() noexcept { return false; }
        auto await_suspend(coroutine_handle<promise_type> h) noexcept {
          assert(h.promise().barrier_ != nullptr);
          auto continuation =
              h.promise().barrier_->arrive(h.promise().getAsyncFrame());
          h.destroy();
          return continuation;
        }
        void await_resume() noexcept {}
      };
      return awaiter{};
    }

    [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }

    void return_void() noexcept {}

    template <typename Awaitable>
    auto await_transform(Awaitable&& awaitable) {
      return folly::coro::co_withAsyncStack(
          static_cast<Awaitable&&>(awaitable));
    }

    void setBarrier(Barrier* barrier) noexcept { barrier_ = barrier; }

    AsyncStackFrame& getAsyncFrame() noexcept { return asyncFrame_; }

   private:
    AsyncStackFrame asyncFrame_;
    Barrier* barrier_;
  };

 private:
  using handle_t = coroutine_handle<promise_type>;

  explicit DetachedBarrierTask(handle_t coro) : coro_(coro) {}

 public:
  DetachedBarrierTask(DetachedBarrierTask&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~DetachedBarrierTask() {
    if (coro_) {
      coro_.destroy();
    }
  }

  FOLLY_NOINLINE void start(Barrier* barrier) && noexcept {
    std::move(*this).start(barrier, FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }

  FOLLY_NOINLINE void start(
      Barrier* barrier, folly::AsyncStackFrame& parentFrame) && noexcept {
    assert(coro_);
    coro_.promise().getAsyncFrame().setParentFrame(parentFrame);
    std::move(*this).start(barrier, FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }

  void start(Barrier* barrier, void* returnAddress) && noexcept {
    assert(coro_);
    assert(barrier != nullptr);
    barrier->add(1);
    auto coro = std::exchange(coro_, {});
    coro.promise().setBarrier(barrier);
    coro.promise().getAsyncFrame().setReturnAddress(returnAddress);
    folly::resumeCoroutineWithNewAsyncStackRoot(coro);
  }

 private:
  handle_t coro_;
};

} // namespace detail
} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
