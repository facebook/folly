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

#include <exception>
#include <experimental/coroutine>
#include <type_traits>

#include <glog/logging.h>

#include <folly/CancellationToken.h>
#include <folly/Executor.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Traits.h>
#include <folly/Try.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Error.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/Utils.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/experimental/coro/detail/Traits.h>
#include <folly/futures/Future.h>
#include <folly/io/async/Request.h>
#include <folly/lang/Assume.h>

namespace folly {
namespace coro {

template <typename T = void>
class Task;

template <typename T = void>
class TaskWithExecutor;

template <typename T>
class co_result final {
 public:
  explicit co_result(Try<T>&& result) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : result_(std::move(result)) {}

  const Try<T>& result() const {
    return result_;
  }

  Try<T>& result() {
    return result_;
  }

 private:
  Try<T> result_;
};

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

    [[noreturn]] void await_resume() noexcept {
      folly::assume_unreachable();
    }
  };

  friend class FinalAwaiter;

 protected:
  TaskPromiseBase() noexcept {}

 public:
  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  std::experimental::suspend_always initial_suspend() noexcept {
    return {};
  }

  FinalAwaiter final_suspend() noexcept {
    return {};
  }

  template <typename Awaitable>
  auto await_transform(Awaitable&& awaitable) {
    return folly::coro::co_viaIfAsync(
        executor_.get_alias(),
        folly::coro::co_withCancellation(
            cancelToken_, static_cast<Awaitable&&>(awaitable)));
  }

  auto await_transform(co_current_executor_t) noexcept {
    return AwaitableReady<folly::Executor*>{executor_.get()};
  }

  auto await_transform(co_current_cancellation_token_t) noexcept {
    return AwaitableReady<const folly::CancellationToken&>{cancelToken_};
  }

  void setCancelToken(const folly::CancellationToken& cancelToken) noexcept {
    if (!hasCancelTokenOverride_) {
      cancelToken_ = cancelToken;
      hasCancelTokenOverride_ = true;
    }
  }

 private:
  template <typename T>
  friend class folly::coro::TaskWithExecutor;

  template <typename T>
  friend class folly::coro::Task;

  std::experimental::coroutine_handle<> continuation_;
  folly::Executor::KeepAlive<> executor_;
  folly::CancellationToken cancelToken_;
  bool hasCancelTokenOverride_ = false;
};

template <typename T>
class TaskPromise : public TaskPromiseBase {
 public:
  static_assert(
      !std::is_rvalue_reference_v<T>,
      "Task<T&&> is not supported. "
      "Consider using Task<T> or Task<std::unique_ptr<T>> instead.");

  using StorageType = detail::lift_lvalue_reference_t<T>;

  TaskPromise() noexcept = default;

  Task<T> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(
        exception_wrapper::from_exception_ptr(std::current_exception()));
  }

  void return_value(T&& t) {
    result_.emplace(static_cast<T&&>(t));
  }

  template <typename U>
  void return_value(U&& value) {
    if constexpr (std::is_same_v<remove_cvref_t<U>, Try<StorageType>>) {
      DCHECK(value.hasValue() || value.hasException());
      result_ = static_cast<U&&>(value);
    } else {
      static_assert(
          std::is_convertible<U&&, StorageType>::value,
          "cannot convert return value to type T");
      result_.emplace(static_cast<U&&>(value));
    }
  }

  Try<StorageType>& result() {
    return result_;
  }

  auto yield_value(co_error ex) {
    result_.emplaceException(std::move(ex.exception()));
    return final_suspend();
  }

  auto yield_value(co_result<StorageType>&& result) {
    result_ = std::move(result.result());
    return final_suspend();
  }

 private:
  Try<StorageType> result_;
};

template <>
class TaskPromise<void> : public TaskPromiseBase {
 public:
  using StorageType = void;

  TaskPromise() noexcept = default;

  Task<void> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(
        exception_wrapper::from_exception_ptr(std::current_exception()));
  }

  void return_void() noexcept {
    result_.emplace();
  }

  Try<void>& result() {
    return result_;
  }

  auto yield_value(co_error ex) {
    result_.emplaceException(std::move(ex.exception()));
    return final_suspend();
  }

  auto yield_value(co_result<void>&& result) {
    result_ = std::move(result.result());
    return final_suspend();
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
  using StorageType = typename detail::TaskPromise<T>::StorageType;

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
    Promise<lift_unit_t<StorageType>> p;

    auto sf = p.getSemiFuture();

    std::move(*this).start(
        [promise = std::move(p)](Try<StorageType>&& result) mutable {
          promise.setTry(std::move(result));
        });

    return sf;
  }

  // Start execution of this task eagerly and call the callback when complete.
  template <typename F>
  void start(F&& tryCallback, folly::CancellationToken cancelToken = {}) && {
    coro_.promise().setCancelToken(std::move(cancelToken));

    [](TaskWithExecutor task,
       std::decay_t<F> cb) -> detail::InlineTaskDetached {
      try {
        cb(co_await std::move(task).co_awaitTry());
      } catch (const std::exception& e) {
        cb(Try<StorageType>(exception_wrapper(std::current_exception(), e)));
      } catch (...) {
        cb(Try<StorageType>(exception_wrapper(std::current_exception())));
      }
    }(std::move(*this), std::forward<F>(tryCallback));
  }

  // Start execution of this task eagerly, inline on the current thread.
  // Assumes that the current thread is already on the associated execution
  // context.
  template <typename F>
  void startInlineUnsafe(
      F&& tryCallback,
      folly::CancellationToken cancelToken = {}) && {
    coro_.promise().setCancelToken(std::move(cancelToken));

    RequestContextScopeGuard contextScope{RequestContext::saveContext()};

    [](TaskWithExecutor task,
       std::decay_t<F> cb) -> detail::InlineTaskDetached {
      try {
        cb(co_await InlineAwaiter{std::exchange(task.coro_, {})});
      } catch (const std::exception& e) {
        cb(Try<StorageType>(exception_wrapper(std::current_exception(), e)));
      } catch (...) {
        cb(Try<StorageType>(exception_wrapper(std::current_exception())));
      }
    }(std::move(*this), std::forward<F>(tryCallback));
  }

  // Start execution of this task eagerly inline on the current thread,
  // assuming the current thread is already on the associated executor,
  // and return a folly::SemiFuture<T> that will complete with the result.
  auto startInlineUnsafe() && {
    Promise<lift_unit_t<StorageType>> p;

    auto sf = p.getSemiFuture();

    std::move(*this).startInlineUnsafe(
        [promise = std::move(p)](Try<StorageType>&& result) mutable {
          promise.setTry(std::move(result));
        });

    return sf;
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

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend(
        std::experimental::coroutine_handle<> continuation) noexcept {
      auto& promise = coro_.promise();
      DCHECK(!promise.continuation_);
      DCHECK(promise.executor_);
      DCHECK(!dynamic_cast<folly::InlineExecutor*>(promise.executor_.get()))
          << "InlineExecutor is not safe and is not supported for coro::Task. "
          << "If you need to run a task inline in a unit-test, you should use "
          << "coro::blockingWait instead.";
      DCHECK(!dynamic_cast<folly::QueuedImmediateExecutor*>(
          promise.executor_.get()))
          << "QueuedImmediateExecutor is not safe and is not supported for coro::Task. "
          << "If you need to run a task inline in a unit-test, you should use "
          << "coro::blockingWait instead.";

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

  class InlineAwaiter {
   public:
    InlineAwaiter(handle_t coro) noexcept : coro_(coro) {}

    ~InlineAwaiter() {
      if (coro_) {
        coro_.destroy();
      }
    }

    bool await_ready() {
      return false;
    }

    auto await_suspend(std::experimental::coroutine_handle<> continuation) {
      auto& promise = coro_.promise();
      DCHECK(!promise.continuation_);
      DCHECK(promise.executor_);

      promise.continuation_ = continuation;
      return coro_;
    }

    folly::Try<StorageType> await_resume() {
      // Eagerly destroy the coroutine-frame once we have retrieved the result.
      SCOPE_EXIT {
        std::exchange(coro_, {}).destroy();
      };
      return std::move(coro_.promise().result());
    }

   private:
    handle_t coro_;
  };

  struct ValueCreator {
    T operator()(Try<StorageType>&& t) const {
      return std::move(t).value();
    }
  };

  struct TryCreator {
    Try<StorageType> operator()(Try<StorageType>&& t) const {
      return std::move(t);
    }
  };

  auto operator co_await() && noexcept {
    return Awaiter<ValueCreator>{std::exchange(coro_, {})};
  }

  auto co_awaitTry() && noexcept {
    return Awaiter<TryCreator>{std::exchange(coro_, {})};
  }

  friend TaskWithExecutor co_withCancellation(
      const folly::CancellationToken& cancelToken,
      TaskWithExecutor&& task) noexcept {
    task.coro_.promise().setCancelToken(cancelToken);
    return std::move(task);
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
/// current RequestContext at the time the coroutine function is either
/// awaited or explicitly started and will save/restore the current
/// RequestContext whenever the coroutine suspends and resumes at a co_await
/// expression.
template <typename T>
class FOLLY_NODISCARD Task {
 public:
  using promise_type = detail::TaskPromise<T>;
  using StorageType = typename promise_type::StorageType;

 private:
  class Awaiter;
  using handle_t = std::experimental::coroutine_handle<promise_type>;

  void setExecutor(folly::Executor::KeepAlive<>&& e) noexcept {
    coro_.promise().executor_ = std::move(e);
  }

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
  TaskWithExecutor<T> scheduleOn(Executor::KeepAlive<> executor) && noexcept {
    setExecutor(std::move(executor));
    return TaskWithExecutor<T>{std::exchange(coro_, {})};
  }

  SemiFuture<folly::lift_unit_t<StorageType>> semi() && {
    return makeSemiFuture().deferExTry(
        [task = std::move(*this)](
            const Executor::KeepAlive<>& executor, Try<Unit>&&) mutable {
          return std::move(task).scheduleOn(executor.get()).start();
        });
  }

  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor,
      Task<T>&& t) noexcept {
    // Child task inherits the awaiting task's executor
    t.setExecutor(std::move(executor));
    return Awaiter{std::exchange(t.coro_, {})};
  }

  friend Task co_withCancellation(
      const folly::CancellationToken& cancelToken,
      Task&& task) noexcept {
    task.coro_.promise().setCancelToken(cancelToken);
    return std::move(task);
  }

  template <typename F, typename... A, typename F_, typename... A_>
  friend Task folly_co_invoke(tag_t<Task, F, A...>, F_ f, A_... a) {
    co_return co_await invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
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
      return await_resume_try().value();
    }

    auto await_resume_try() {
      SCOPE_EXIT {
        std::exchange(coro_, {}).destroy();
      };
      return std::move(coro_.promise().result());
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

} // namespace coro
} // namespace folly
