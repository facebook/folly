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

#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/Malloc.h>

#include <cassert>
#include <experimental/coroutine>
#include <utility>

namespace folly {
namespace coro {
namespace detail {

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
    static void* operator new(std::size_t size) {
      return ::folly_coro_async_malloc(size);
    }

    static void operator delete(void* ptr, std::size_t size) {
      ::folly_coro_async_free(ptr, size);
    }

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

class DetachedBarrierTask {
 public:
  class promise_type {
   public:
    DetachedBarrierTask get_return_object() noexcept {
      return DetachedBarrierTask{
          std::experimental::coroutine_handle<promise_type>::from_promise(
              *this)};
    }

    std::experimental::suspend_always initial_suspend() noexcept {
      return {};
    }

    auto final_suspend() noexcept {
      struct awaiter {
        bool await_ready() noexcept {
          return false;
        }
        auto await_suspend(
            std::experimental::coroutine_handle<promise_type> h) noexcept {
          assert(h.promise().barrier_ != nullptr);
          auto continuation = h.promise().barrier_->arrive();
          h.destroy();
          return continuation;
        }
        void await_resume() noexcept {}
      };
      return awaiter{};
    }

    [[noreturn]] void unhandled_exception() noexcept {
      std::terminate();
    }

    void return_void() noexcept {}

    void setBarrier(Barrier* barrier) noexcept {
      barrier_ = barrier;
    }

   private:
    Barrier* barrier_;
  };

 private:
  using handle_t = std::experimental::coroutine_handle<promise_type>;

  explicit DetachedBarrierTask(handle_t coro) : coro_(coro) {}

 public:
  DetachedBarrierTask(DetachedBarrierTask&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~DetachedBarrierTask() {
    if (coro_) {
      coro_.destroy();
    }
  }

  void start(Barrier* barrier) && noexcept {
    assert(coro_);
    assert(barrier != nullptr);
    barrier->add(1);
    auto coro = std::exchange(coro_, {});
    coro.promise().setBarrier(barrier);
    coro.resume();
  }

 private:
  handle_t coro_;
};

} // namespace detail
} // namespace coro
} // namespace folly
