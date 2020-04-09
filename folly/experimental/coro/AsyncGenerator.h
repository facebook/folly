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

#include <folly/CancellationToken.h>
#include <folly/Traits.h>
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Utils.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/experimental/coro/detail/ManualLifetime.h>

#include <glog/logging.h>

#include <experimental/coroutine>
#include <iterator>
#include <type_traits>

namespace folly {
namespace coro {

template <typename Reference, typename Value>
class AsyncGenerator;

namespace detail {

template <typename Reference, typename Value>
class AsyncGeneratorPromise {
  class YieldAwaiter {
   public:
    bool await_ready() noexcept {
      return false;
    }
    std::experimental::coroutine_handle<> await_suspend(
        std::experimental::coroutine_handle<AsyncGeneratorPromise> h) noexcept {
      return h.promise().continuation_;
    }
    void await_resume() noexcept {}
  };

 public:
  ~AsyncGeneratorPromise() {
    if (hasValue_) {
      value_.destruct();
    }
  }

  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  AsyncGenerator<Reference, Value> get_return_object() noexcept;

  std::experimental::suspend_always initial_suspend() noexcept {
    return {};
  }

  YieldAwaiter final_suspend() noexcept {
    DCHECK(!hasValue_);
    clearContext();
    return {};
  }

  YieldAwaiter yield_value(Reference&& value) noexcept(
      std::is_nothrow_move_constructible<Reference>::value) {
    DCHECK(!hasValue_);
    value_.construct(static_cast<Reference&&>(value));
    hasValue_ = true;
    clearContext();
    return YieldAwaiter{};
  }

  // In the case where 'Reference' is not actually a reference-type we
  // allow implicit conversion from the co_yield argument to Reference.
  // However, we don't want to allow this for cases where 'Reference' _is_
  // a reference because this could result in the reference binding to a
  // temporary that results from an implicit conversion.
  template <
      typename U,
      std::enable_if_t<
          !std::is_reference_v<Reference> &&
              std::is_convertible_v<U&&, Reference>,
          int> = 0>
  YieldAwaiter yield_value(U&& value) noexcept(
      std::is_nothrow_constructible_v<Reference, U>) {
    DCHECK(!hasValue_);
    value_.construct(static_cast<U&&>(value));
    hasValue_ = true;
    clearContext();
    return {};
  }

  void unhandled_exception() noexcept {
    DCHECK(!hasValue_);
    exception_ = std::current_exception();
  }

  void return_void() noexcept {
    DCHECK(!hasValue_);
  }

  template <typename U>
  auto await_transform(U&& value) {
    return folly::coro::co_viaIfAsync(
        executor_.get_alias(),
        folly::coro::co_withCancellation(
            cancelToken_, static_cast<U&&>(value)));
  }

  auto await_transform(folly::coro::co_current_executor_t) noexcept {
    return AwaitableReady<folly::Executor*>{executor_.get()};
  }

  auto await_transform(folly::coro::co_current_cancellation_token_t) noexcept {
    return AwaitableReady<folly::CancellationToken>{cancelToken_};
  }

  void setCancellationToken(folly::CancellationToken cancelToken) noexcept {
    // Only keep the first cancellation token.
    // ie. the inner-most cancellation scope of the consumer's calling context.
    if (!hasCancelTokenOverride_) {
      cancelToken_ = std::move(cancelToken);
      hasCancelTokenOverride_ = true;
    }
  }

  void setExecutor(folly::Executor::KeepAlive<> executor) noexcept {
    executor_ = std::move(executor);
  }

  void setContinuation(
      std::experimental::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
  }

  void throwIfException() {
    DCHECK(!hasValue_);
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

  decltype(auto) getRvalue() noexcept {
    DCHECK(hasValue_);
    return std::move(value_).get();
  }

  void clearValue() noexcept {
    if (hasValue_) {
      hasValue_ = false;
      value_.destruct();
    }
  }

  bool hasValue() const noexcept {
    return hasValue_;
  }

 private:
  void clearContext() noexcept {
    executor_ = {};
    cancelToken_ = {};
    hasCancelTokenOverride_ = false;
  }

  std::experimental::coroutine_handle<> continuation_;
  folly::Executor::KeepAlive<> executor_;
  folly::CancellationToken cancelToken_;
  std::exception_ptr exception_;
  ManualLifetime<Reference> value_;
  bool hasValue_ = false;
  bool hasCancelTokenOverride_ = false;
};

} // namespace detail

// The AsyncGenerator class represents a sequence of asynchronously produced
// values where the values are produced by a coroutine.
//
// Values are produced by using the 'co_yield' keyword and the coroutine can
// also consume other asynchronous operations using the 'co_await' keyword.
// The end of the sequence is indicated by executing 'co_return;' either
// explicitly or by letting execution run off the end of the coroutine.
//
// Reference Type
// --------------
// The first template parameter controls the 'reference' type.
// i.e. the type returned when you dereference the iterator using operator*().
// This type is typically specified as an actual reference type.
// eg. 'const T&' (non-mutable), 'T&' (mutable)  or 'T&&' (movable) depending
// what access you want your consumers to have to the yielded values.
//
// It's also possible to specify the 'Reference' template parameter as a value
// type. In this case the generator takes a copy of the yielded value (either
// copied or move-constructed) and you get a copy of this value every time
// you dereference the iterator with '*iter'.
// This can be expensive for types that are expensive to copy, but can provide
// a small performance win for types that are cheap to copy (like built-in
// integer types).
//
// Value Type
// ----------
// The second template parameter is optional, but if specified can be used as
// the value-type that should be used to take a copy of the value returned by
// the Reference type.
// By default this type is the same as 'Reference' type stripped of qualifiers
// and references. However, in some cases it can be a different type.
// For example, if the 'Reference' type was a non-reference proxy type.
//
// Example:
//  AsyncGenerator<std::tuple<const K&, V&>, std::tuple<K, V>> getItems() {
//    auto firstMap = co_await getFirstMap();
//    for (auto&& [k, v] : firstMap) {
//      co_yield {k, v};
//    }
//    auto secondMap = co_await getSecondMap();
//    for (auto&& [k, v] : secondMap) {
//      co_yield {k, v};
//    }
//  }
//
// This is mostly useful for generic algorithms that need to take copies of
// elements of the sequence.
//
// Executor Affinity
// -----------------
// An AsyncGenerator coroutine has similar executor-affinity to that of the
// folly::coro::Task coroutine type. Every time a consumer requests a new value
// from the generator using 'co_await ++it' the generator inherits the caller's
// current executor. The coroutine will ensure that it always resumes on the
// associated executor when resuming from `co_await' expression until it hits
// the next 'co_yield' or 'co_return' statement.
// Note that the executor can potentially change at a 'co_yield' statement if
// the next element of the sequence is requested from a consumer coroutine that
// is associated with a different executor.
//
// Example: Writing an async generator.
//
//  folly::coro::AsyncGenerator<Record&&> getRecordsAsync() {
//    auto resultSet = executeQuery(someQuery);
//    for (;;) {
//      auto resultSetPage = co_await resultSet.nextPage();
//      if (resultSetPage.empty()) break;
//      for (auto& row : resultSetPage) {
//        co_yield Record{row.get("name"), row.get("email")};
//      }
//    }
//  }
//
// Example: Consuming items from an async generator
//
//  folly::coro::Task<void> consumer() {
//    auto records = getRecordsAsync();
//    while (auto item = co_await records.next()) {
//      auto&& record = *item;
//      process(record);
//    }
//  }
//
template <typename Reference, typename Value = remove_cvref_t<Reference>>
class FOLLY_NODISCARD AsyncGenerator {
  static_assert(
      std::is_constructible<Value, Reference>::value,
      "AsyncGenerator 'value_type' must be constructible from a 'reference'.");

 public:
  using promise_type = detail::AsyncGeneratorPromise<Reference, Value>;

 private:
  using handle_t = std::experimental::coroutine_handle<promise_type>;

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
      coro_.destroy();
    }
  }

  AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
    auto oldCoro = std::exchange(coro_, std::exchange(other.coro_, {}));
    if (oldCoro) {
      oldCoro.destroy();
    }
    return *this;
  }

  void swap(AsyncGenerator& other) noexcept {
    std::swap(coro_, other.coro_);
  }

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

    bool has_value() const noexcept {
      return hasValue_;
    }

    explicit operator bool() const noexcept {
      return has_value();
    }

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

    decltype(auto) operator*() & {
      return value();
    }

    decltype(auto) operator*() && {
      return std::move(*this).value();
    }

    decltype(auto) operator*() const& {
      return value();
    }

    decltype(auto) operator*() const&& {
      return std::move(*this).value();
    }

    decltype(auto) operator-> () {
      DCHECK(has_value());
      auto&& x = value_.get();
      return std::addressof(x);
    }

    decltype(auto) operator-> () const {
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
    bool await_ready() {
      return !coro_;
    }

    handle_t await_suspend(
        std::experimental::coroutine_handle<> continuation) noexcept {
      auto& promise = coro_.promise();
      promise.setContinuation(continuation);
      promise.clearValue();
      return coro_;
    }

    NextResult await_resume() {
      if (!coro_) {
        return NextResult{};
      } else if (coro_.done()) {
        coro_.promise().throwIfException();
        return NextResult{};
      } else {
        return NextResult{coro_};
      }
    }

   private:
    friend NextSemiAwaitable;
    explicit NextAwaitable(handle_t coro) noexcept : coro_(coro) {}

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
        CancellationToken cancelToken,
        NextSemiAwaitable&& awaitable) {
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
  friend AsyncGenerator
  folly_co_invoke(tag_t<AsyncGenerator, F, A...>, F_ f, A_... a) {
    auto r = invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
    while (auto v = co_await r.next()) {
      co_yield std::move(v).value();
    }
  }

 private:
  friend class detail::AsyncGeneratorPromise<Reference, Value>;

  explicit AsyncGenerator(
      std::experimental::coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  std::experimental::coroutine_handle<promise_type> coro_;
};

namespace detail {

template <typename Reference, typename Value>
AsyncGenerator<Reference, Value>
AsyncGeneratorPromise<Reference, Value>::get_return_object() noexcept {
  return AsyncGenerator<Reference, Value>{std::experimental::coroutine_handle<
      AsyncGeneratorPromise<Reference, Value>>::from_promise(*this)};
}

} // namespace detail

} // namespace coro
} // namespace folly
