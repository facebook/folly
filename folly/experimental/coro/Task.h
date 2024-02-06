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

//
// Docs: https://fburl.com/fbcref_coro_task
//

#pragma once

#include <exception>
#include <type_traits>

#include <glog/logging.h>

#include <folly/CancellationToken.h>
#include <folly/Executor.h>
#include <folly/GLog.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Traits.h>
#include <folly/Try.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Result.h>
#include <folly/experimental/coro/ScopeExit.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/experimental/coro/detail/Traits.h>
#include <folly/futures/Future.h>
#include <folly/io/async/Request.h>
#include <folly/lang/Assume.h>
#include <folly/tracing/AsyncStack.h>

#if FOLLY_HAS_COROUTINES

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
    bool await_ready() noexcept { return false; }

    template <typename Promise>
    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES coroutine_handle<>
    await_suspend(coroutine_handle<Promise> coro) noexcept {
      auto& promise = coro.promise();
      // If ScopeExitTask has been attached, then we expect that the
      // ScopeExitTask will handle the lifetime of the async stack. See
      // ScopeExitTaskPromise's FinalAwaiter for more details.
      //
      // This is a bit untidy, and hopefully something we can replace with
      // a virtual wrapper over coroutine_handle that handles the pop for us.
      if (promise.scopeExit_) {
        promise.scopeExit_.promise().setContext(
            promise.continuation_,
            &promise.asyncFrame_,
            promise.executor_.get_alias(),
            promise.result_.hasException() ? promise.result_.exception()
                                           : exception_wrapper{});
        return promise.scopeExit_;
      }

      folly::popAsyncStackFrameCallee(promise.asyncFrame_);
      if (promise.result_.hasException()) {
        auto [handle, frame] =
            promise.continuation_.getErrorHandle(promise.result_.exception());
        return handle.getHandle();
      }
      return promise.continuation_.getHandle();
    }

    [[noreturn]] void await_resume() noexcept { folly::assume_unreachable(); }
  };

  friend class FinalAwaiter;

 protected:
  TaskPromiseBase() noexcept = default;
  ~TaskPromiseBase() = default;

  template <typename Promise>
  variant_awaitable<FinalAwaiter, ready_awaitable<>> do_safe_point(
      Promise& promise) noexcept {
    if (cancelToken_.isCancellationRequested()) {
      return promise.yield_value(co_cancelled);
    }
    return ready_awaitable<>{};
  }

 public:
  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  suspend_always initial_suspend() noexcept { return {}; }

  FinalAwaiter final_suspend() noexcept { return {}; }

  template <typename Awaitable>
  auto await_transform(Awaitable&& awaitable) {
    bypassExceptionThrowing_ =
        bypassExceptionThrowing_ == BypassExceptionThrowing::REQUESTED
        ? BypassExceptionThrowing::ACTIVE
        : BypassExceptionThrowing::INACTIVE;

    return folly::coro::co_withAsyncStack(folly::coro::co_viaIfAsync(
        executor_.get_alias(),
        folly::coro::co_withCancellation(
            cancelToken_, static_cast<Awaitable&&>(awaitable))));
  }

  template <typename Awaitable>
  auto await_transform(NothrowAwaitable<Awaitable>&& awaitable) {
    bypassExceptionThrowing_ = BypassExceptionThrowing::REQUESTED;
    return await_transform(awaitable.unwrap());
  }

  auto await_transform(co_current_executor_t) noexcept {
    return ready_awaitable<folly::Executor*>{executor_.get()};
  }

  auto await_transform(co_current_cancellation_token_t) noexcept {
    return ready_awaitable<const folly::CancellationToken&>{cancelToken_};
  }

  void setCancelToken(folly::CancellationToken&& cancelToken) noexcept {
    if (!hasCancelTokenOverride_) {
      cancelToken_ = std::move(cancelToken);
      hasCancelTokenOverride_ = true;
    }
  }

  folly::AsyncStackFrame& getAsyncFrame() noexcept { return asyncFrame_; }

  folly::Executor::KeepAlive<> getExecutor() const noexcept {
    return executor_;
  }

 private:
  template <typename T>
  friend class folly::coro::TaskWithExecutor;

  template <typename T>
  friend class folly::coro::Task;

  friend coroutine_handle<ScopeExitTaskPromiseBase> tag_invoke(
      cpo_t<co_attachScopeExit>,
      TaskPromiseBase& p,
      coroutine_handle<ScopeExitTaskPromiseBase> scopeExit) noexcept {
    return std::exchange(p.scopeExit_, scopeExit);
  }

  ExtendedCoroutineHandle continuation_;
  folly::AsyncStackFrame asyncFrame_;
  folly::Executor::KeepAlive<> executor_;
  folly::CancellationToken cancelToken_;
  coroutine_handle<ScopeExitTaskPromiseBase> scopeExit_;
  bool hasCancelTokenOverride_ = false;

 protected:
  enum class BypassExceptionThrowing : uint8_t {
    INACTIVE,
    ACTIVE,
    REQUESTED,
  } bypassExceptionThrowing_{BypassExceptionThrowing::INACTIVE};
};

template <typename T>
class TaskPromise final : public TaskPromiseBase,
                          public ExtendedCoroutinePromiseImpl<TaskPromise<T>> {
 public:
  static_assert(
      !std::is_rvalue_reference_v<T>,
      "Task<T&&> is not supported. "
      "Consider using Task<T> or Task<std::unique_ptr<T>> instead.");
  friend class TaskPromiseBase;

  using StorageType = detail::lift_lvalue_reference_t<T>;

  TaskPromise() noexcept = default;

  Task<T> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(exception_wrapper{std::current_exception()});
  }

  template <typename U = T>
  void return_value(U&& value) {
    if constexpr (std::is_same_v<remove_cvref_t<U>, Try<StorageType>>) {
      DCHECK(value.hasValue() || (value.hasException() && value.exception()));
      result_ = static_cast<U&&>(value);
    } else if constexpr (
        std::is_same_v<remove_cvref_t<U>, Try<void>> &&
        std::is_same_v<remove_cvref_t<T>, Unit>) {
      // special-case to make task -> semifuture -> task preserve void type
      DCHECK(value.hasValue() || (value.hasException() && value.exception()));
      result_ = static_cast<Try<Unit>>(static_cast<U&&>(value));
    } else {
      static_assert(
          std::is_convertible<U&&, StorageType>::value,
          "cannot convert return value to type T");
      result_.emplace(static_cast<U&&>(value));
    }
  }

  Try<StorageType>& result() { return result_; }

  auto yield_value(co_error ex) {
    result_.emplaceException(std::move(ex.exception()));
    return final_suspend();
  }

  auto yield_value(co_result<StorageType>&& result) {
    result_ = std::move(result.result());
    return final_suspend();
  }

  using TaskPromiseBase::await_transform;

  auto await_transform(co_safe_point_t) noexcept {
    return do_safe_point(*this);
  }

  std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper& ex) override {
    if (bypassExceptionThrowing_ == BypassExceptionThrowing::ACTIVE) {
      auto finalAwaiter = yield_value(co_error(std::move(ex)));
      DCHECK(!finalAwaiter.await_ready());
      return {
          finalAwaiter.await_suspend(
              coroutine_handle<TaskPromise>::from_promise(*this)),
          // finalAwaiter.await_suspend pops a frame
          getAsyncFrame().getParentFrame()};
    }
    return {coroutine_handle<TaskPromise>::from_promise(*this), nullptr};
  }

 private:
  Try<StorageType> result_;
};

template <>
class TaskPromise<void> final
    : public TaskPromiseBase,
      public ExtendedCoroutinePromiseImpl<TaskPromise<void>> {
 public:
  friend class TaskPromiseBase;

  using StorageType = void;

  TaskPromise() noexcept = default;

  Task<void> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    result_.emplaceException(exception_wrapper{std::current_exception()});
  }

  void return_void() noexcept { result_.emplace(); }

  Try<void>& result() { return result_; }

  auto yield_value(co_error ex) {
    result_.emplaceException(std::move(ex.exception()));
    return final_suspend();
  }

  auto yield_value(co_result<void>&& result) {
    result_ = std::move(result.result());
    return final_suspend();
  }
  auto yield_value(co_result<Unit>&& result) {
    result_ = std::move(result.result());
    return final_suspend();
  }

  using TaskPromiseBase::await_transform;

  auto await_transform(co_safe_point_t) noexcept {
    return do_safe_point(*this);
  }

  std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper& ex) override {
    if (bypassExceptionThrowing_ == BypassExceptionThrowing::ACTIVE) {
      auto finalAwaiter = yield_value(co_error(std::move(ex)));
      DCHECK(!finalAwaiter.await_ready());
      return {
          finalAwaiter.await_suspend(
              coroutine_handle<TaskPromise>::from_promise(*this)),
          // finalAwaiter.await_suspend pops a frame
          getAsyncFrame().getParentFrame()};
    }
    return {coroutine_handle<TaskPromise>::from_promise(*this), nullptr};
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
///
/// More information on how to use this is available at folly::coro::Task.
template <typename T>
class FOLLY_NODISCARD TaskWithExecutor {
  using handle_t = coroutine_handle<detail::TaskPromise<T>>;
  using StorageType = typename detail::TaskPromise<T>::StorageType;

 public:
  /// @private
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
  /// Returns the executor that the task is bound to
  folly::Executor* executor() const noexcept {
    return coro_.promise().executor_.get();
  }

  void swap(TaskWithExecutor& t) noexcept { std::swap(coro_, t.coro_); }

  /// Start eager execution of this task.
  ///
  /// This starts execution of the Task on the bound executor.
  /// @returns folly::SemiFuture<T> that will complete with the result.
  FOLLY_NOINLINE SemiFuture<lift_unit_t<StorageType>> start() && {
    folly::Promise<lift_unit_t<StorageType>> p;

    auto sf = p.getSemiFuture();

    std::move(*this).startImpl(
        [promise = std::move(p)](Try<StorageType>&& result) mutable {
          promise.setTry(std::move(result));
        },
        folly::CancellationToken{},
        FOLLY_ASYNC_STACK_RETURN_ADDRESS());

    return sf;
  }

  /// Start eager execution of the task and call the passed callback on
  /// completion
  ///
  /// This starts execution of the Task on the bound executor, and call the
  /// passed callback upon completion. The callback takes a Try<T> which
  /// represents either th value returned by the Task on success or an
  /// exeception thrown by the Task
  /// @param tryCallback a function that takes in a Try<T>
  /// @param cancelToken a CancelationToken object
  template <typename F>
  FOLLY_NOINLINE void start(
      F&& tryCallback, folly::CancellationToken cancelToken = {}) && {
    std::move(*this).startImpl(
        static_cast<F&&>(tryCallback),
        std::move(cancelToken),
        FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }

  /// Start eager execution of this task on this thread.
  ///
  /// Assumes the current thread is already on the executor associated with the
  /// Task. Refer to TaskWithExecuter::start(F&& tryCallback,
  /// folly::CancellationToken cancelToken = {}) for more information.
  template <typename F>
  FOLLY_NOINLINE void startInlineUnsafe(
      F&& tryCallback, folly::CancellationToken cancelToken = {}) && {
    std::move(*this).startInlineImpl(
        static_cast<F&&>(tryCallback),
        std::move(cancelToken),
        FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }

  /// Start eager execution of this task on this thread.
  ///
  /// Assumes the current thread is already on the executor associated with the
  /// Task. Refer to TaskWithExecuter::start() for more information.
  FOLLY_NOINLINE SemiFuture<lift_unit_t<StorageType>> startInlineUnsafe() && {
    folly::Promise<lift_unit_t<StorageType>> p;

    auto sf = p.getSemiFuture();

    std::move(*this).startInlineImpl(
        [promise = std::move(p)](Try<StorageType>&& result) mutable {
          promise.setTry(std::move(result));
        },
        folly::CancellationToken{},
        FOLLY_ASYNC_STACK_RETURN_ADDRESS());

    return sf;
  }

 private:
  template <typename F>
  void startImpl(
      F&& tryCallback,
      folly::CancellationToken cancelToken,
      void* returnAddress) && {
    coro_.promise().setCancelToken(std::move(cancelToken));
    startImpl(std::move(*this), static_cast<F&&>(tryCallback))
        .start(returnAddress);
  }

  template <typename F>
  void startInlineImpl(
      F&& tryCallback,
      folly::CancellationToken cancelToken,
      void* returnAddress) && {
    coro_.promise().setCancelToken(std::move(cancelToken));
    RequestContextScopeGuard contextScope{RequestContext::saveContext()};
    startInlineImpl(std::move(*this), static_cast<F&&>(tryCallback))
        .start(returnAddress);
  }

  template <typename F>
  detail::InlineTaskDetached startImpl(TaskWithExecutor task, F cb) {
    try {
      cb(co_await folly::coro::co_awaitTry(std::move(task)));
    } catch (...) {
      cb(Try<StorageType>(exception_wrapper(std::current_exception())));
    }
  }

  template <typename F>
  detail::InlineTaskDetached startInlineImpl(TaskWithExecutor task, F cb) {
    try {
      cb(co_await InlineTryAwaitable{std::exchange(task.coro_, {})});
    } catch (...) {
      cb(Try<StorageType>(exception_wrapper(std::current_exception())));
    }
  }

 public:
  class Awaiter {
   public:
    explicit Awaiter(handle_t coro) noexcept : coro_(coro) {}

    Awaiter(Awaiter&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

    ~Awaiter() {
      if (coro_) {
        coro_.destroy();
      }
    }

    bool await_ready() const { return false; }

    template <typename Promise>
    FOLLY_NOINLINE void await_suspend(
        coroutine_handle<Promise> continuation) noexcept {
      DCHECK(coro_);
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
      if constexpr (kIsDebug) {
        if (dynamic_cast<InlineLikeExecutor*>(promise.executor_.get())) {
          FB_LOG_ONCE(ERROR)
              << "InlineLikeExecutor is not safe and is not supported for coro::Task. "
              << "If you need to run a task inline in a unit-test, you should use "
              << "coro::blockingWait or write your test using the CO_TEST* macros instead."
              << "If you are using folly::getCPUExecutor, switch to getGlobalCPUExecutor "
              << "or be sure to call setCPUExecutor first.";
        }
      }

      auto& calleeFrame = promise.getAsyncFrame();
      calleeFrame.setReturnAddress();

      if constexpr (detail::promiseHasAsyncFrame_v<Promise>) {
        auto& callerFrame = continuation.promise().getAsyncFrame();
        calleeFrame.setParentFrame(callerFrame);
        folly::deactivateAsyncStackFrame(callerFrame);
      }

      promise.continuation_ = continuation;
      promise.executor_->add(
          [coro = coro_, ctx = RequestContext::saveContext()]() mutable {
            RequestContextScopeGuard contextScope{std::move(ctx)};
            folly::resumeCoroutineWithNewAsyncStackRoot(coro);
          });
    }

    T await_resume() {
      DCHECK(coro_);
      // Eagerly destroy the coroutine-frame once we have retrieved the result.
      SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
      return std::move(coro_.promise().result()).value();
    }

    folly::Try<StorageType> await_resume_try() {
      SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
      return std::move(coro_.promise().result());
    }

   private:
    handle_t coro_;
  };

  class InlineTryAwaitable {
   public:
    InlineTryAwaitable(handle_t coro) noexcept : coro_(coro) {}

    InlineTryAwaitable(InlineTryAwaitable&& other) noexcept
        : coro_(std::exchange(other.coro_, {})) {}

    ~InlineTryAwaitable() {
      if (coro_) {
        coro_.destroy();
      }
    }

    bool await_ready() { return false; }

    template <typename Promise>
    FOLLY_NOINLINE coroutine_handle<> await_suspend(
        coroutine_handle<Promise> continuation) {
      DCHECK(coro_);
      auto& promise = coro_.promise();
      DCHECK(!promise.continuation_);
      DCHECK(promise.executor_);

      promise.continuation_ = continuation;

      auto& calleeFrame = promise.getAsyncFrame();
      calleeFrame.setReturnAddress();

      // This awaitable is only ever awaited from a DetachedInlineTask
      // which is an async-stack-aware coroutine.
      //
      // Assume it has a .getAsyncFrame() and that this frame is currently
      // active.
      auto& callerFrame = continuation.promise().getAsyncFrame();
      folly::pushAsyncStackFrameCallerCallee(callerFrame, calleeFrame);
      return coro_;
    }

    folly::Try<StorageType> await_resume() {
      DCHECK(coro_);
      // Eagerly destroy the coroutine-frame once we have retrieved the result.
      SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
      return std::move(coro_.promise().result());
    }

   private:
    friend InlineTryAwaitable tag_invoke(
        cpo_t<co_withAsyncStack>, InlineTryAwaitable&& awaitable) noexcept {
      return std::move(awaitable);
    }

    handle_t coro_;
  };

 public:
  Awaiter operator co_await() && noexcept {
    DCHECK(coro_);
    return Awaiter{std::exchange(coro_, {})};
  }

  friend TaskWithExecutor co_withCancellation(
      folly::CancellationToken cancelToken, TaskWithExecutor&& task) noexcept {
    DCHECK(task.coro_);
    task.coro_.promise().setCancelToken(std::move(cancelToken));
    return std::move(task);
  }

  friend TaskWithExecutor tag_invoke(
      cpo_t<co_withAsyncStack>, TaskWithExecutor&& task) noexcept {
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
/// Within the body of a Task's coroutine, executor binding to the parent
/// executor is maintained by implicitly transforming all 'co_await expr'
/// expressions into `co_await co_viaIfAsync(parentExecutor, expr)' to ensure
/// that the coroutine always resumes on the parent's executor.
///
/// The Task coroutine is RequestContext-aware
/// and will capture the current RequestContext at the time the coroutine
/// function is either awaited or explicitly started and will save/restore the
/// current RequestContext whenever the coroutine suspends and resumes at a
/// co_await expression.
///
/// More documentation on how to use coroutines is available at
/// https://github.com/facebook/folly/blob/main/folly/experimental/coro/README.md
///
/// @refcode folly/docs/examples/folly/experimental/coro/Task.cpp
template <typename T>
class FOLLY_NODISCARD Task {
 public:
  using promise_type = detail::TaskPromise<T>;
  using StorageType = typename promise_type::StorageType;

 private:
  class Awaiter;
  using handle_t = coroutine_handle<promise_type>;

  void setExecutor(folly::Executor::KeepAlive<>&& e) noexcept {
    DCHECK(coro_);
    DCHECK(e);
    coro_.promise().executor_ = std::move(e);
  }

 public:
  Task(const Task& t) = delete;

  /// Create a Task, invalidating the original Task in the process.
  Task(Task&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}

  /// @private
  ~Task() {
    if (coro_) {
      coro_.destroy();
    }
  }

  Task& operator=(Task t) noexcept {
    swap(t);
    return *this;
  }

  void swap(Task& t) noexcept { std::swap(coro_, t.coro_); }

  /// Specify the executor that this task should execute on.
  /// @param executor An Executor::KeepAlive object, which can be implicity
  /// constructed from Executor
  /// @returns a new TaskWithExecutor object, which represents the existing Task
  /// bound to an executor
  FOLLY_NODISCARD
  TaskWithExecutor<T> scheduleOn(Executor::KeepAlive<> executor) && noexcept {
    setExecutor(std::move(executor));
    DCHECK(coro_);
    return TaskWithExecutor<T>{std::exchange(coro_, {})};
  }

  /// Converts a Task into a SemiFuture object.
  ///
  /// The SemiFuture object is implicitly of type Semifuture<Try<T>>, where the
  /// Try represents whether the execution of the converted Task succeeded and T
  /// is the original task's result type.
  /// @returns a SemiFuture object
  FOLLY_NOINLINE
  SemiFuture<folly::lift_unit_t<StorageType>> semi() && {
    return makeSemiFuture().deferExTry(
        [task = std::move(*this),
         returnAddress = FOLLY_ASYNC_STACK_RETURN_ADDRESS()](
            const Executor::KeepAlive<>& executor, Try<Unit>&&) mutable {
          folly::Promise<lift_unit_t<StorageType>> p;

          auto sf = p.getSemiFuture();

          std::move(task).scheduleOn(executor).startInlineImpl(
              [promise = std::move(p)](Try<StorageType>&& result) mutable {
                promise.setTry(std::move(result));
              },
              folly::CancellationToken{},
              returnAddress);

          return sf;
        });
  }

  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor, Task<T>&& t) noexcept {
    DCHECK(t.coro_);
    // Child task inherits the awaiting task's executor
    t.setExecutor(std::move(executor));
    return Awaiter{std::exchange(t.coro_, {})};
  }

  friend Task co_withCancellation(
      folly::CancellationToken cancelToken, Task&& task) noexcept {
    DCHECK(task.coro_);
    task.coro_.promise().setCancelToken(std::move(cancelToken));
    return std::move(task);
  }

  template <typename F, typename... A, typename F_, typename... A_>
  friend Task tag_invoke(
      tag_t<co_invoke_fn>, tag_t<Task, F, A...>, F_ f, A_... a) {
    co_yield co_result(co_await co_awaitTry(
        invoke(static_cast<F&&>(f), static_cast<A&&>(a)...)));
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

    bool await_ready() noexcept { return false; }

    template <typename Promise>
    FOLLY_NOINLINE auto await_suspend(
        coroutine_handle<Promise> continuation) noexcept {
      DCHECK(coro_);
      auto& promise = coro_.promise();

      promise.continuation_ = continuation;

      auto& calleeFrame = promise.getAsyncFrame();
      calleeFrame.setReturnAddress();

      if constexpr (detail::promiseHasAsyncFrame_v<Promise>) {
        auto& callerFrame = continuation.promise().getAsyncFrame();
        folly::pushAsyncStackFrameCallerCallee(callerFrame, calleeFrame);
        return coro_;
      } else {
        folly::resumeCoroutineWithNewAsyncStackRoot(coro_);
        return;
      }
    }

    T await_resume() {
      DCHECK(coro_);
      SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
      return std::move(coro_.promise().result()).value();
    }

    folly::Try<StorageType> await_resume_try() {
      DCHECK(coro_);
      SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
      return std::move(coro_.promise().result());
    }

   private:
    // This overload needed as Awaiter is returned from co_viaIfAsync() which is
    // then passed into co_withAsyncStack().
    friend Awaiter tag_invoke(
        cpo_t<co_withAsyncStack>, Awaiter&& awaiter) noexcept {
      return std::move(awaiter);
    }

    handle_t coro_;
  };

  Task(handle_t coro) noexcept : coro_(coro) {}

  handle_t coro_;
};

/// Make a task that trivially returns a value.
/// @param t value to be returned by the Task
template <class T>
Task<T> makeTask(T t) {
  co_return t;
}

/// Make a Task that trivially returns with no return value.
inline Task<void> makeTask() {
  co_return;
}
/// Same as makeTask(). See Unit
inline Task<void> makeTask(Unit) {
  co_return;
}

/// Make a Task that will trivially yield an Exception.
/// @param ew an exception_wrapper object
template <class T>
Task<T> makeErrorTask(exception_wrapper ew) {
  co_yield co_error(std::move(ew));
}

/// Make a Task out of a Try.
/// @tparam T the type of the value wrapped by the Try
/// @param t the Try to convert into a Task
/// @returns a Task that will yield the Try's value or exeception.
template <class T>
Task<drop_unit_t<T>> makeResultTask(Try<T> t) {
  co_yield co_result(std::move(t));
}

template <typename T>
Task<T> detail::TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{coroutine_handle<detail::TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> detail::TaskPromise<void>::get_return_object() noexcept {
  return Task<void>{
      coroutine_handle<detail::TaskPromise<void>>::from_promise(*this)};
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
