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
// Docs: https://fburl.com/fbcref_asyncgenerator
//

#pragma once

#include <folly/CancellationToken.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Traits.h>
#include <folly/Try.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Result.h>
#include <folly/experimental/coro/ScopeExit.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/experimental/coro/detail/ManualLifetime.h>
#include <folly/tracing/AsyncStack.h>

#include <glog/logging.h>

#include <iterator>
#include <type_traits>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

template <typename Reference, typename Value, bool RequiresCleanup>
class AsyncGeneratorPromise;

} // namespace detail

/**
 * The AsyncGenerator class represents a sequence of asynchronously produced
 * values where the values are produced by a coroutine.
 *
 * Values are produced by using the 'co_yield' keyword and the coroutine can
 * also consume other asynchronous operations using the 'co_await' keyword.
 * The end of the sequence is indicated by executing 'co_return;' either
 * explicitly or by letting execution run off the end of the coroutine.
 *
 * Reference Type
 * --------------
 * The first template parameter controls the 'reference' type.
 * i.e. the type returned when you dereference the iterator using operator*().
 * This type is typically specified as an actual reference type.
 * eg. 'const T&' (non-mutable), 'T&' (mutable)  or 'T&&' (movable) depending
 * what access you want your consumers to have to the yielded values.
 *
 * It's also possible to specify the 'Reference' template parameter as a value
 * type. In this case the generator takes a copy of the yielded value (either
 * copied or move-constructed) and you get a copy of this value every time
 * you dereference the iterator with '*iter'.
 * This can be expensive for types that are expensive to copy, but can provide
 * a small performance win for types that are cheap to copy (like built-in
 * integer types).
 *
 * Value Type
 * ----------
 * The second template parameter is optional, but if specified can be used as
 * the value-type that should be used to take a copy of the value returned by
 * the Reference type.
 * By default this type is the same as 'Reference' type stripped of qualifiers
 * and references. However, in some cases it can be a different type.
 * For example, if the 'Reference' type was a non-reference proxy type.
 *
 * Example:
 *
 *     AsyncGenerator<std::tuple<const K&, V&>, std::tuple<K, V>> getItems() {
 *         auto firstMap = co_await getFirstMap();
 *         for (auto&& [k, v] : firstMap) {
 *           co_yield {k, v};
 *         }
 *         auto secondMap = co_await getSecondMap();
 *         for (auto&& [k, v] : secondMap) {
 *           co_yield {k, v};
 *         }
 *      }
 *
 * This is mostly useful for generic algorithms that need to take copies of
 * elements of the sequence.
 *
 * Executor Affinity
 * -----------------
 * An AsyncGenerator coroutine has similar executor-affinity to that of the
 * folly::coro::Task coroutine type. Every time a consumer requests a new value
 * from the generator using 'co_await ++it' the generator inherits the caller's
 * current executor. The coroutine will ensure that it always resumes on the
 * associated executor when resuming from `co_await' expression until it hits
 * the next 'co_yield' or 'co_return' statement.
 * Note that the executor can potentially change at a 'co_yield' statement if
 * the next element of the sequence is requested from a consumer coroutine that
 * is associated with a different executor.
 *
 * Example: Writing an async generator.
 *
 *       folly::coro::AsyncGenerator<Record&&> getRecordsAsync() {
 *         auto resultSet = executeQuery(someQuery);
 *         for (;;) {
 *           auto resultSetPage = co_await resultSet.nextPage();
 *           if (resultSetPage.empty()) break;
 *           for (auto& row : resultSetPage) {
 *             co_yield Record{row.get("name"), row.get("email")};
 *           }
 *         }
 *  }
 *
 * Example: Consuming items from an async generator
 *
 *       folly::coro::Task<void> consumer() {
 *         auto records = getRecordsAsync();
 *         while (auto item = co_await records.next()) {
 *           auto&& record = *item;
 *           process(record);
 *         }
 *       }
 *
 * Async Cleanup
 * -------------
 * When the template parameter RequiresCleanup is true, the owner of an
 * AsyncGenerator is responsible for awaiting cleanup() before the generator
 * object's destructor is called. That allows to use folly::coro::co_scope_exit
 * awaitables inside AsyncGenerator, which are asynchronously executed when
 * cleanup() is awaited. Note that the AsyncGenerator coroutine frame is
 * destroyed before co_scope_exit awaitables are executed.
 *
 * There is an alias CleanableAsyncGenerator for AsyncGenerator with
 * RequiresCleanup set to true.
 */
template <
    typename Reference,
    typename Value = remove_cvref_t<Reference>,
    bool RequiresCleanup = false>
class FOLLY_NODISCARD AsyncGenerator {
  static_assert(
      std::is_constructible<Value, Reference>::value,
      "AsyncGenerator 'value_type' must be constructible from a 'reference'.");

 public:
  using promise_type =
      detail::AsyncGeneratorPromise<Reference, Value, RequiresCleanup>;

 private:
  using handle_t = coroutine_handle<promise_type>;

 public:
  using value_type = Value;
  using reference = Reference;
  using pointer = std::add_pointer_t<Reference>;

 public:
  AsyncGenerator() noexcept : coro_() {}

  AsyncGenerator(AsyncGenerator&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~AsyncGenerator() {
    if (coro_) {
      CHECK(!RequiresCleanup) << "cleanup() hasn't been called!";
      coro_.destroy();
    }
  }

  class CleanupSemiAwaitable;

  class FOLLY_NODISCARD CleanupAwaitable {
   public:
    bool await_ready() noexcept { return !scopeExit_; }

    template <typename Promise>
    FOLLY_NOINLINE auto await_suspend(
        coroutine_handle<Promise> continuation) noexcept {
      asyncFrame_.setReturnAddress();
      scopeExit_.promise().setContext(
          continuation, &asyncFrame_, executor_.get_alias());
      if constexpr (detail::promiseHasAsyncFrame_v<Promise>) {
        folly::pushAsyncStackFrameCallerCallee(
            continuation.promise().getAsyncFrame(), asyncFrame_);
        return scopeExit_;
      } else {
        folly::resumeCoroutineWithNewAsyncStackRoot(scopeExit_);
      }
    }

    void await_resume() noexcept {}

   private:
    friend CleanupSemiAwaitable;

    CleanupAwaitable(
        coroutine_handle<detail::ScopeExitTaskPromiseBase> scopeExit,
        folly::Executor::KeepAlive<> executor) noexcept
        : scopeExit_{scopeExit}, executor_{std::move(executor)} {}

    friend CleanupAwaitable tag_invoke(
        cpo_t<co_withAsyncStack>, CleanupAwaitable awaitable) noexcept {
      return std::move(awaitable);
    }

    coroutine_handle<detail::ScopeExitTaskPromiseBase> scopeExit_;
    folly::AsyncStackFrame asyncFrame_;
    folly::Executor::KeepAlive<> executor_;
  };

  class FOLLY_NODISCARD CleanupSemiAwaitable {
   public:
    CleanupAwaitable viaIfAsync(Executor::KeepAlive<> executor) noexcept {
      return CleanupAwaitable{scopeExit_, std::move(executor)};
    }

   private:
    friend AsyncGenerator;

    explicit CleanupSemiAwaitable(
        coroutine_handle<detail::ScopeExitTaskPromiseBase> scopeExit) noexcept
        : scopeExit_{scopeExit} {}

    coroutine_handle<detail::ScopeExitTaskPromiseBase> scopeExit_;
  };

  CleanupSemiAwaitable cleanup() && {
    static_assert(RequiresCleanup);
    CHECK(coro_) << "cleanup() has been already called!";
    SCOPE_EXIT { std::exchange(coro_, {}).destroy(); };
    return CleanupSemiAwaitable{coro_.promise().scopeExit_};
  }

  AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
    auto oldCoro = std::exchange(coro_, std::exchange(other.coro_, {}));
    if (oldCoro) {
      CHECK(!RequiresCleanup) << "cleanup() hasn't been called!";
      oldCoro.destroy();
    }
    return *this;
  }

  void swap(AsyncGenerator& other) noexcept { std::swap(coro_, other.coro_); }

  class NextAwaitable;
  class NextSemiAwaitable;

  class NextResult {
   public:
    NextResult() noexcept : hasValue_(false) {}

    NextResult(NextResult&& other) noexcept : hasValue_(other.hasValue_) {
      if (hasValue_) {
        value_.construct(std::move(other.value_).get());
      }
    }

    ~NextResult() {
      if (hasValue_) {
        value_.destruct();
      }
    }

    NextResult& operator=(NextResult&& other) {
      if (&other != this) {
        if (has_value()) {
          hasValue_ = false;
          value_.destruct();
        }

        if (other.has_value()) {
          value_.construct(std::move(other.value_).get());
          hasValue_ = true;
        }
      }
      return *this;
    }

    bool has_value() const noexcept { return hasValue_; }

    explicit operator bool() const noexcept { return has_value(); }

    decltype(auto) value() & {
      DCHECK(has_value());
      return value_.get();
    }

    decltype(auto) value() && {
      DCHECK(has_value());
      return std::move(value_).get();
    }

    decltype(auto) value() const& {
      DCHECK(has_value());
      return value_.get();
    }

    decltype(auto) value() const&& {
      DCHECK(has_value());
      return std::move(value_).get();
    }

    decltype(auto) operator*() & { return value(); }

    decltype(auto) operator*() && { return std::move(*this).value(); }

    decltype(auto) operator*() const& { return value(); }

    decltype(auto) operator*() const&& { return std::move(*this).value(); }

    decltype(auto) operator->() {
      DCHECK(has_value());
      auto&& x = value_.get();
      return std::addressof(x);
    }

    decltype(auto) operator->() const {
      DCHECK(has_value());
      auto&& x = value_.get();
      return std::addressof(x);
    }

   private:
    friend NextAwaitable;
    explicit NextResult(handle_t coro) noexcept : hasValue_(true) {
      value_.construct(coro.promise().getRvalue());
    }

    detail::ManualLifetime<Reference> value_;
    bool hasValue_ = false;
  };

  class NextAwaitable {
   public:
    bool await_ready() noexcept { return !coro_; }

    template <typename Promise>
    FOLLY_NOINLINE auto await_suspend(
        coroutine_handle<Promise> continuation) noexcept {
      auto& promise = coro_.promise();

      promise.setContinuation(continuation);
      promise.clearValue();

      auto& asyncFrame = promise.getAsyncFrame();
      asyncFrame.setReturnAddress();

      if constexpr (detail::promiseHasAsyncFrame_v<Promise>) {
        folly::pushAsyncStackFrameCallerCallee(
            continuation.promise().getAsyncFrame(), asyncFrame);
        return coro_;
      } else {
        folly::resumeCoroutineWithNewAsyncStackRoot(coro_);
      }
    }

    NextResult await_resume() {
      if (!coro_) {
        return NextResult{};
      } else if (!coro_.promise().hasValue()) {
        coro_.promise().throwIfException();
        return NextResult{};
      } else {
        return NextResult{coro_};
      }
    }

    folly::Try<NextResult> await_resume_try() {
      if (coro_) {
        if (coro_.promise().hasValue()) {
          return folly::Try<NextResult>(NextResult{coro_});
        } else if (coro_.promise().hasException()) {
          return folly::Try<NextResult>(
              std::move(coro_.promise().getException()));
        }
      }
      return folly::Try<NextResult>(NextResult{});
    }

   private:
    friend NextSemiAwaitable;
    explicit NextAwaitable(handle_t coro) noexcept : coro_(coro) {}

    friend NextAwaitable tag_invoke(
        cpo_t<co_withAsyncStack>, NextAwaitable awaitable) noexcept {
      return NextAwaitable{awaitable.coro_};
    }

    handle_t coro_;
  };

  class NextSemiAwaitable {
   public:
    NextAwaitable viaIfAsync(Executor::KeepAlive<> executor) noexcept {
      if (coro_) {
        coro_.promise().setExecutor(std::move(executor));
      }
      return NextAwaitable{coro_};
    }

    friend NextSemiAwaitable co_withCancellation(
        CancellationToken cancelToken, NextSemiAwaitable&& awaitable) {
      if (awaitable.coro_) {
        awaitable.coro_.promise().setCancellationToken(std::move(cancelToken));
      }
      return NextSemiAwaitable{std::exchange(awaitable.coro_, {})};
    }

   private:
    friend AsyncGenerator;

    explicit NextSemiAwaitable(handle_t coro) noexcept : coro_(coro) {}

    handle_t coro_;
  };

  NextSemiAwaitable next() noexcept {
    DCHECK(!coro_ || !coro_.done());
    return NextSemiAwaitable{coro_};
  }

  template <typename F, typename... A, typename F_, typename... A_>
  friend AsyncGenerator tag_invoke(
      tag_t<co_invoke_fn>, tag_t<AsyncGenerator, F, A...>, F_ f, A_... a) {
    if constexpr (RequiresCleanup) {
      auto&& [r] = co_await co_scope_exit(
          [](auto&& gen) { return std::move(gen).cleanup(); },
          invoke(static_cast<F&&>(f), static_cast<A&&>(a)...));
      while (true) {
        co_yield co_result(co_await co_awaitTry(r.next()));
      }
    } else {
      auto r = invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
      while (true) {
        co_yield co_result(co_await co_awaitTry(r.next()));
      }
    }
  }

 private:
  friend promise_type;

  explicit AsyncGenerator(coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  coroutine_handle<promise_type> coro_;
};

template <typename Reference, typename Value = remove_cvref_t<Reference>>
using CleanableAsyncGenerator =
    AsyncGenerator<Reference, Value, true /* RequiresCleanup */>;

namespace detail {

template <bool RequiresCleanup>
struct BaseAsyncGeneratorPromise {};

template <>
struct BaseAsyncGeneratorPromise<true> {
  coroutine_handle<ScopeExitTaskPromiseBase> scopeExit_;
};

template <typename Reference, typename Value, bool RequiresCleanup = false>
class AsyncGeneratorPromise final
    : public ExtendedCoroutinePromiseImpl<
          AsyncGeneratorPromise<Reference, Value, RequiresCleanup>>,
      BaseAsyncGeneratorPromise<RequiresCleanup> {
  class YieldAwaiter {
   public:
    bool await_ready() noexcept { return false; }
    coroutine_handle<> await_suspend(
        coroutine_handle<AsyncGeneratorPromise> h) noexcept {
      AsyncGeneratorPromise& promise = h.promise();
      // Pop AsyncStackFrame first as clearContext() clears the frame state.
      folly::popAsyncStackFrameCallee(promise.getAsyncFrame());
      promise.clearContext();
      if (promise.hasException()) {
        auto [handle, frame] =
            promise.continuation_.getErrorHandle(promise.getException());
        return handle.getHandle();
      }
      return promise.continuation_.getHandle();
    }
    void await_resume() noexcept {}
  };

 public:
  AsyncGeneratorPromise() noexcept {}

  ~AsyncGeneratorPromise() {
    switch (state_) {
      case State::VALUE:
        folly::coro::detail::deactivate(value_);
        break;
      case State::EXCEPTION_WRAPPER:
        folly::coro::detail::deactivate(exceptionWrapper_);
        break;
      case State::DONE:
      case State::INVALID:
        break;
    }
  }

  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  AsyncGenerator<Reference, Value, RequiresCleanup>
  get_return_object() noexcept {
    return AsyncGenerator<Reference, Value, RequiresCleanup>{
        coroutine_handle<AsyncGeneratorPromise>::from_promise(*this)};
  }

  suspend_always initial_suspend() noexcept { return {}; }

  YieldAwaiter final_suspend() noexcept {
    DCHECK(!hasValue());
    return {};
  }

  YieldAwaiter yield_value(Reference&& value) noexcept(
      std::is_nothrow_move_constructible<Reference>::value) {
    DCHECK(state_ == State::INVALID);
    folly::coro::detail::activate(value_, static_cast<Reference&&>(value));
    state_ = State::VALUE;
    return YieldAwaiter{};
  }

  /// In the case where 'Reference' is not actually a reference-type we
  /// allow implicit conversion from the co_yield argument to Reference.
  /// However, we don't want to allow this for cases where 'Reference' _is_
  /// a reference because this could result in the reference binding to a
  /// temporary that results from an implicit conversion.
  template <
      typename U,
      std::enable_if_t<
          !std::is_reference_v<Reference> &&
              std::is_convertible_v<U&&, Reference>,
          int> = 0>
  YieldAwaiter yield_value(U&& value) noexcept(
      std::is_nothrow_constructible_v<Reference, U>) {
    DCHECK(state_ == State::INVALID);
    folly::coro::detail::activate(value_, static_cast<U&&>(value));
    state_ = State::VALUE;
    return {};
  }

  YieldAwaiter yield_value(co_error&& error) noexcept {
    DCHECK(state_ == State::INVALID);
    folly::coro::detail::activate(
        exceptionWrapper_, std::move(error.exception()));
    state_ = State::EXCEPTION_WRAPPER;
    return {};
  }

  YieldAwaiter yield_value(co_result<Value>&& res) noexcept {
    if (res.result().hasValue()) {
      return yield_value(std::move(res.result().value()));
    } else if (res.result().hasException()) {
      return yield_value(co_error(res.result().exception()));
    } else {
      return_void();
      return {};
    }
  }

  YieldAwaiter yield_value(
      co_result<typename AsyncGenerator<Reference, Value, RequiresCleanup>::
                    NextResult>&& res) noexcept {
    DCHECK(
        res.result().hasValue() ||
        (res.result().hasException() && res.result().exception()));
    if (res.result().hasException()) {
      return yield_value(co_error(res.result().exception()));
    } else if (res.result().hasValue()) {
      if (res.result()->has_value()) {
        return yield_value(std::move(res.result()->value()));
      } else {
        return_void();
        return {};
      }
    }
    return yield_value(co_error(UsingUninitializedTry{}));
  }

  variant_awaitable<YieldAwaiter, ready_awaitable<>> await_transform(
      co_safe_point_t) noexcept {
    if (cancelToken_.isCancellationRequested()) {
      return yield_value(co_cancelled);
    }
    return ready_awaitable<>{};
  }

  void unhandled_exception() noexcept {
    DCHECK(state_ == State::INVALID);
    folly::coro::detail::activate(exceptionWrapper_, std::current_exception());
    state_ = State::EXCEPTION_WRAPPER;
  }

  void return_void() noexcept {
    DCHECK(state_ == State::INVALID);
    state_ = State::DONE;
  }

  template <typename U>
  auto await_transform(U&& value) {
    bypassExceptionThrowing_ =
        bypassExceptionThrowing_ == BypassExceptionThrowing::REQUESTED
        ? BypassExceptionThrowing::ACTIVE
        : BypassExceptionThrowing::INACTIVE;
    return folly::coro::co_withAsyncStack(folly::coro::co_viaIfAsync(
        executor_.get_alias(),
        folly::coro::co_withCancellation(
            cancelToken_, static_cast<U&&>(value))));
  }

  template <typename Awaitable>
  auto await_transform(NothrowAwaitable<Awaitable>&& awaitable) {
    bypassExceptionThrowing_ = BypassExceptionThrowing::REQUESTED;
    return await_transform(awaitable.unwrap());
  }

  auto await_transform(folly::coro::co_current_executor_t) noexcept {
    return ready_awaitable<folly::Executor*>{executor_.get()};
  }

  auto await_transform(folly::coro::co_current_cancellation_token_t) noexcept {
    return ready_awaitable<const folly::CancellationToken&>{cancelToken_};
  }

  void setCancellationToken(folly::CancellationToken cancelToken) noexcept {
    // Only keep the first cancellation token.
    // ie. the inner-most cancellation scope of the consumer's calling
    // context.
    if (!hasCancelTokenOverride_) {
      cancelToken_ = std::move(cancelToken);
      hasCancelTokenOverride_ = true;
    }
  }

  void setExecutor(folly::Executor::KeepAlive<> executor) noexcept {
    DCHECK(executor);
    executor_ = std::move(executor);
  }

  void setContinuation(ExtendedCoroutineHandle continuation) noexcept {
    continuation_ = continuation;
  }

  bool hasException() const noexcept {
    return state_ == State::EXCEPTION_WRAPPER;
  }

  folly::exception_wrapper& getException() noexcept {
    DCHECK(hasException());
    return exceptionWrapper_.get();
  }

  void throwIfException() {
    if (state_ == State::EXCEPTION_WRAPPER) {
      exceptionWrapper_.get().throw_exception();
    }
  }

  decltype(auto) getRvalue() noexcept {
    DCHECK(hasValue());
    return std::move(value_).get();
  }

  void clearValue() noexcept {
    if (hasValue()) {
      state_ = State::INVALID;
      folly::coro::detail::deactivate(value_);
    }
  }

  bool hasValue() const noexcept { return state_ == State::VALUE; }

  folly::AsyncStackFrame& getAsyncFrame() noexcept { return asyncFrame_; }

  std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper& ex) override {
    if (bypassExceptionThrowing_ == BypassExceptionThrowing::ACTIVE) {
      auto yieldAwaiter = yield_value(co_error(std::move(ex)));
      DCHECK(!yieldAwaiter.await_ready());
      return {
          yieldAwaiter.await_suspend(
              coroutine_handle<AsyncGeneratorPromise>::from_promise(*this)),
          // yieldAwaiter.await_suspend pops a frame
          getAsyncFrame().getParentFrame()};
    }
    return {
        coroutine_handle<AsyncGeneratorPromise>::from_promise(*this), nullptr};
  }

 private:
  friend AsyncGenerator<Reference, Value, RequiresCleanup>;

  void clearContext() noexcept {
    executor_ = {};
    cancelToken_ = {};
    hasCancelTokenOverride_ = false;
    asyncFrame_ = {};
  }

  friend coroutine_handle<ScopeExitTaskPromiseBase> tag_invoke(
      cpo_t<co_attachScopeExit>,
      AsyncGeneratorPromise& p,
      coroutine_handle<ScopeExitTaskPromiseBase> scopeExit) noexcept {
    static_assert(
        RequiresCleanup,
        "Only CleanableAsyncGenerator (AsyncGenerator with RequiresCleanup"
        " template parameter set to true) supports attaching co_scope_exit");
    return std::exchange(p.scopeExit_, scopeExit);
  }

  enum class State : std::uint8_t {
    INVALID,
    VALUE,
    EXCEPTION_WRAPPER,
    DONE,
  };

  ExtendedCoroutineHandle continuation_;
  folly::AsyncStackFrame asyncFrame_;
  folly::Executor::KeepAlive<> executor_;
  folly::CancellationToken cancelToken_;
  union {
    ManualLifetime<folly::exception_wrapper> exceptionWrapper_;
    ManualLifetime<Reference> value_;
  };
  State state_ = State::INVALID;
  bool hasCancelTokenOverride_ = false;

  enum class BypassExceptionThrowing : uint8_t {
    INACTIVE,
    ACTIVE,
    REQUESTED,
  } bypassExceptionThrowing_{BypassExceptionThrowing::INACTIVE};
};

} // namespace detail
} // namespace coro
} // namespace folly

#endif
