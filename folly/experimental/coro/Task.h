/*
 * Copyright 2017-present Facebook, Inc.
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

#include <experimental/coroutine>
#include <type_traits>

#include <glog/logging.h>

#include <folly/Executor.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Try.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/Utils.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/futures/Future.h>
#include <folly/io/async/Request.h>

namespace folly {
namespace coro {

template <typename T = void>
class Task;

template <typename T = void>
class TaskWithExecutor;

namespace detail {

class TaskPromiseBase {
  class FinalAwaiter {
   public:
    bool await_ready() noexcept {
      return false;
    }

    template <typename Promise>
    std::experimental::coroutine_handle<> await_suspend(
        std::experimental::coroutine_handle<Promise> coro) noexcept {
      TaskPromiseBase& promise = coro.promise();
      return promise.continuation_;
    }

    void await_resume() noexcept {}
  };

  friend class FinalAwaiter;

 protected:
  TaskPromiseBase() noexcept {}

 public:
  std::experimental::suspend_always initial_suspend() noexcept {
    return {};
  }

  FinalAwaiter final_suspend() noexcept {
    return {};
  }

  template <typename Awaitable>
  auto await_transform(Awaitable&& awaitable) noexcept {
    return folly::coro::co_viaIfAsync(
        executor_.copyDummy(), static_cast<Awaitable&&>(awaitable));
  }

  auto await_transform(co_current_executor_t) noexcept {
    return AwaitableReady<folly::Executor*>{executor_.get()};
  }

 private:
  template <typename T>
  friend class folly::coro::TaskWithExecutor;

  template <typename T>
  friend class folly::coro::Task;

  std::experimental::coroutine_handle<> continuation_;
  folly::Executor::KeepAlive<> executor_;
};

template <typename T>
class TaskPromise : public TaskPromiseBase {
 public:
  TaskPromise() noexcept = default;

  Task<T> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(
        exception_wrapper::from_exception_ptr(std::current_exception()));
  }

  template <typename U>
  void return_value(U&& value) {
    static_assert(
        std::is_convertible<U&&, T>::value,
        "cannot convert return value to type T");
    result_.emplace(static_cast<U&&>(value));
  }

  Try<T>& result() {
    return result_;
  }

 private:
  using StorageType = std::conditional_t<
      std::is_reference<T>::value,
      std::reference_wrapper<std::remove_reference_t<T>>,
      T>;

  Try<StorageType> result_;
};

template <>
class TaskPromise<void> : public TaskPromiseBase {
 public:
  TaskPromise() noexcept = default;

  Task<void> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(
        exception_wrapper::from_exception_ptr(std::current_exception()));
  }

  void return_void() noexcept {}

  Try<void>& result() {
    return result_;
  }

 private:
  Try<void> result_;
};

} // namespace detail

/// Represents an allocated but not yet started coroutine that has already
/// been bound to an executor.
///
/// This task, when co_awaited, will launch the task on the bound executor
/// and will resume the awaiting coroutine on the bound executor when it
/// completes.
template <typename T>
class FOLLY_NODISCARD TaskWithExecutor {
  using handle_t = std::experimental::coroutine_handle<detail::TaskPromise<T>>;

 public:
  ~TaskWithExecutor() {
    if (coro_) {
      coro_.destroy();
    }
  }

  TaskWithExecutor(TaskWithExecutor&& t) noexcept
      : coro_(std::exchange(t.coro_, {})) {}

  TaskWithExecutor& operator=(TaskWithExecutor t) noexcept {
    swap(t);
    return *this;
  }

  folly::Executor* executor() const noexcept {
    return coro_.promise().executor_.get();
  }

  void swap(TaskWithExecutor& t) noexcept {
    std::swap(coro_, t.coro_);
  }

  // Start execution of this task eagerly and return a folly::SemiFuture<T>
  // that will complete with the result.
  auto start() && {
    Promise<lift_unit_t<T>> p;
    auto sf = p.getSemiFuture();

    std::move(*this).start([promise = std::move(p)](Try<T>&& result) mutable {
      promise.setTry(std::move(result));
    });

    return sf;
  }

  // Start execution of this task eagerly and call the callback when complete.
  template <typename F>
  void start(F&& tryCallback) && {
    [](TaskWithExecutor task,
       std::decay_t<F> cb) -> detail::InlineTaskDetached {
      try {
        cb(co_await std::move(task).co_awaitTry());
      } catch (const std::exception& e) {
        cb(Try<T>(exception_wrapper(std::current_exception(), e)));
      } catch (...) {
        cb(Try<T>(exception_wrapper(std::current_exception())));
      }
    }(std::move(*this), std::forward<F>(tryCallback));
  }

  template <typename ResultCreator>
  class Awaiter {
   public:
    explicit Awaiter(handle_t coro) noexcept : coro_(coro) {}

    ~Awaiter() {
      if (coro_) {
        coro_.destroy();
      }
    }

    bool await_ready() const {
      return false;
    }

    void await_suspend(
        std::experimental::coroutine_handle<> continuation) noexcept {
      auto& promise = coro_.promise();
      DCHECK(!promise.continuation_);
      DCHECK(promise.executor_);

      promise.continuation_ = continuation;
      promise.executor_->add(
          [coro = coro_, ctx = RequestContext::saveContext()]() mutable {
            RequestContextScopeGuard contextScope{std::move(ctx)};
            coro.resume();
          });
    }

    decltype(auto) await_resume() {
      // Eagerly destroy the coroutine-frame once we have retrieved the result.
      SCOPE_EXIT {
        std::exchange(coro_, {}).destroy();
      };
      ResultCreator resultCreator;
      return resultCreator(std::move(coro_.promise().result()));
    }

   private:
    handle_t coro_;
  };

  struct ValueCreator {
    T operator()(Try<T>&& t) {
      return std::move(t).value();
    }
  };

  struct TryCreator {
    Try<T> operator()(Try<T>&& t) {
      return std::move(t);
    }
  };

  auto operator co_await() && noexcept {
    return Awaiter<ValueCreator>{std::exchange(coro_, {})};
  }

  auto co_awaitTry() && noexcept {
    return Awaiter<TryCreator>{std::exchange(coro_, {})};
  }

 private:
  friend class Task<T>;

  explicit TaskWithExecutor(handle_t coro) noexcept : coro_(coro) {}

  handle_t coro_;
};

/// Represents an allocated, but not-started coroutine, which is not yet
/// been bound to an executor.
///
/// You can only co_await a Task from within another Task, in which case it
/// is implicitly bound to the same executor as the parent Task.
///
/// Alternatively, you can explicitly provide an executor by calling the
/// task.scheduleOn(executor) method, which will return a new not-yet-started
/// TaskWithExecutor that can be co_awaited anywhere and that will automatically
/// schedule the coroutine to start executing on the bound executor when it
/// is co_awaited.
///
/// Within the body of a Task's coroutine, it will ensure that it always
/// executes on the bound executor by implicitly transforming every
/// 'co_await expr' expression into
/// `co_await co_viaIfAsync(boundExecutor, expr)' to ensure that the coroutine
/// always resumes on the executor.
///
/// The Task coroutine is RequestContext-aware and will capture the
/// current RequestContext at the time the coroutine function is called and
/// will save/restore the current RequestContext whenever the coroutine
/// suspends and resumes at a co_await expression.
template <typename T>
class FOLLY_NODISCARD Task {
 public:
  using promise_type = detail::TaskPromise<T>;

 private:
  class Awaiter;
  using handle_t = std::experimental::coroutine_handle<promise_type>;

 public:
  Task(const Task& t) = delete;

  Task(Task&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

  ~Task() {
    if (coro_) {
      coro_.destroy();
    }
  }

  Task& operator=(Task t) noexcept {
    swap(t);
    return *this;
  }

  void swap(Task& t) noexcept {
    std::swap(coro_, t.coro_);
  }

  /// Specify the executor that this task should execute on.
  ///
  /// Returns a new task that when co_awaited will launch execution of this
  /// task on the specified executor.
  FOLLY_NODISCARD
  TaskWithExecutor<T> scheduleOn(Executor* executor) && noexcept {
    coro_.promise().executor_ = getKeepAliveToken(executor);
    return TaskWithExecutor<T>{std::exchange(coro_, {})};
  }

  SemiFuture<folly::lift_unit_t<T>> semi() && {
    return makeSemiFuture().defer(
        [task = std::move(*this)](Executor* executor, Try<Unit>&&) mutable {
          return std::move(task).scheduleOn(executor).start();
        });
  }

  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor,
      Task<T>&& t) noexcept {
    // Child task inherits the awaiting task's executor
    t.coro_.promise().executor_ = std::move(executor);
    return Awaiter{std::exchange(t.coro_, {})};
  }

 private:
  friend class detail::TaskPromiseBase;
  friend class detail::TaskPromise<T>;

  class Awaiter {
   public:
    explicit Awaiter(handle_t coro) noexcept : coro_(coro) {}

    Awaiter(Awaiter&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

    Awaiter(const Awaiter&) = delete;

    ~Awaiter() {
      if (coro_) {
        coro_.destroy();
      }
    }

    bool await_ready() noexcept {
      return false;
    }

    handle_t await_suspend(
        std::experimental::coroutine_handle<> continuation) noexcept {
      coro_.promise().continuation_ = continuation;
      return coro_;
    }

    T await_resume() {
      SCOPE_EXIT {
        std::exchange(coro_, {}).destroy();
      };
      return std::move(coro_.promise().result()).value();
    }

   private:
    handle_t coro_;
  };

  Task(handle_t coro) noexcept : coro_(coro) {}

  handle_t coro_;
};

template <typename T>
Task<T> detail::TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{
      std::experimental::coroutine_handle<detail::TaskPromise<T>>::from_promise(
          *this)};
}

inline Task<void> detail::TaskPromise<void>::get_return_object() noexcept {
  return Task<void>{std::experimental::coroutine_handle<
      detail::TaskPromise<void>>::from_promise(*this)};
}

namespace detail {
template <typename T>
struct is_task : std::false_type {};
template <typename T>
struct is_task<Task<T>> : std::true_type {};

template <typename F, typename... A, typename F_, typename... A_>
invoke_result_t<F, A...> co_invoke_(F_ f, A_... a) {
  co_return co_await folly::invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
}
} // namespace detail

/// co_invoke
///
/// This utility function is a safe way to instantiate a coroutine using a
/// coroutine callable. It guarantees that the callable and the arguments
/// outlive the coroutine which invocation returns. Otherwise, the callable
/// and the arguments are not safe to be used within the coroutine body.
///
/// For example, if the callable is a lambda with captures, the captures would
/// not otherwise be safe to use in the coroutine body without using co_invoke.
///
/// Models std::invoke for any callable object which returns Task<_>.
///
/// Like std::invoke in that the callable is invoked with the cvref-qual with
/// which it is passed to co_invoke and the arguments are passed with the cvref-
/// quals with which they were passed to co_invoke.
///
/// Different from std::invoke in that the callable and all arguments are decay-
/// copied and held for the lifetime of the coroutine, whereas std::invoke never
/// never constructs anything from the callable or the arguments.
template <typename F, typename... A>
std::enable_if_t<
    detail::is_task<invoke_result_t<F, A...>>::value,
    invoke_result_t<F, A...>>
co_invoke(F&& f, A&&... a) {
  return detail::co_invoke_<F, A...>(
      static_cast<F&&>(f), static_cast<A&&>(a)...);
}

} // namespace coro
} // namespace folly
