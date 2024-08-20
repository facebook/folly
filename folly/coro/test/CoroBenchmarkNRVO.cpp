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

#include <folly/Benchmark.h>

#include <folly/experimental/coro/Coroutine.h>

#include <future>
#include <thread>

struct ExpensiveCopy {
  ExpensiveCopy() {}

  ExpensiveCopy(const ExpensiveCopy&) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  ExpensiveCopy& operator=(const ExpensiveCopy&) = default;
};

#if FOLLY_HAS_COROUTINES

class Wait {
 public:
  class promise_type {
   public:
    Wait get_return_object() { return Wait(promise_.get_future()); }

    folly::coro::suspend_never initial_suspend() noexcept { return {}; }

    folly::coro::suspend_never final_suspend() noexcept { return {}; }

    void return_void() { promise_.set_value(); }

    void unhandled_exception() {
      promise_.set_exception(std::current_exception());
    }

   private:
    std::promise<void> promise_;
  };

  explicit Wait(std::future<void> future) : future_(std::move(future)) {}

  Wait(Wait&&) = default;

  void detach() { future_ = {}; }

  ~Wait() {
    if (future_.valid()) {
      future_.get();
    }
  }

 private:
  std::future<void> future_;
};

template <typename T>
class InlineTask {
 public:
  InlineTask(const InlineTask&) = delete;
  InlineTask(InlineTask&& other)
      : promise_(std::exchange(other.promise_, nullptr)) {}

  ~InlineTask() { DCHECK(!promise_); }

  bool await_ready() const { return false; }

  folly::coro::coroutine_handle<> await_suspend(
      folly::coro::coroutine_handle<> awaiter) {
    promise_->valuePtr_ = &value_;
    promise_->awaiter_ = std::move(awaiter);
    return folly::coro::coroutine_handle<promise_type>::from_promise(*promise_);
  }

  T await_resume() {
    folly::coro::coroutine_handle<promise_type>::from_promise(
        *std::exchange(promise_, nullptr))
        .destroy();
    T value = std::move(value_);
    return value;
  }

  class promise_type {
   public:
    InlineTask get_return_object() { return InlineTask(this); }

    template <typename U = T>
    void return_value(U&& value) {
      *valuePtr_ = std::forward<U>(value);
    }

    void unhandled_exception() { std::terminate(); }

    folly::coro::suspend_always initial_suspend() { return {}; }

    class FinalSuspender {
     public:
      explicit FinalSuspender(folly::coro::coroutine_handle<> awaiter) noexcept
          : awaiter_(std::move(awaiter)) {}

      bool await_ready() noexcept { return false; }

      auto await_suspend(folly::coro::coroutine_handle<>) noexcept {
        return awaiter_;
      }

      void await_resume() noexcept {}

     private:
      folly::coro::coroutine_handle<> awaiter_;
    };

    FinalSuspender final_suspend() noexcept {
      return FinalSuspender(std::move(awaiter_));
    }

   private:
    friend class InlineTask;

    T* valuePtr_;
    folly::coro::coroutine_handle<> awaiter_;
  };

 private:
  friend class promise_type;

  explicit InlineTask(promise_type* promise) : promise_(promise) {}

  T value_;
  promise_type* promise_;
};

InlineTask<ExpensiveCopy> co_nestedCalls(size_t times) {
  ExpensiveCopy ret;
  if (times > 0) {
    ret = co_await co_nestedCalls(times - 1);
  }
  co_return ret;
}

void coroNRVO(size_t times, size_t iters) {
  for (size_t iter = 0; iter < iters; ++iter) {
    [](size_t times) -> Wait {
      (void)co_await co_nestedCalls(times);
      co_return;
    }(times);
  }
}

BENCHMARK(coroNRVOOneAwait, iters) {
  coroNRVO(1, iters);
}

BENCHMARK(coroNRVOTenAwaits, iters) {
  coroNRVO(10, iters);
}
#endif

ExpensiveCopy nestedCalls(size_t times) {
  ExpensiveCopy ret;
  if (times > 0) {
    ret = nestedCalls(times - 1);
  }

  return ret;
}

void NRVO(size_t times, size_t iters) {
  for (size_t iter = 0; iter < iters; ++iter) {
    nestedCalls(times);
  }
}

BENCHMARK(NRVOOneAwait, iters) {
  NRVO(1, iters);
}

BENCHMARK(NRVOTenAwaits, iters) {
  NRVO(10, iters);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
