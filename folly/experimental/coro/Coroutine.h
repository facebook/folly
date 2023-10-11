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

#pragma once

#include <type_traits>

#if __has_include(<variant>)
#include <variant>
#endif

#include <folly/Portability.h>
#include <folly/Utility.h>

#if FOLLY_HAS_COROUTINES

// libc++'s <coroutine> header only provides its declarations for C++20 and
// above, so we need to fall back to <experimental/coroutine> when building with
// C++17.
#if __has_include(<coroutine>) && !defined(LLVM_COROUTINES) && \
    (!defined(_LIBCPP_VERSION) || __cplusplus > 201703L)
#define FOLLY_USE_STD_COROUTINE 1
#else
#define FOLLY_USE_STD_COROUTINE 0
#endif

#if FOLLY_USE_STD_COROUTINE
#include <coroutine>
#else
#include <experimental/coroutine>
#endif

#endif // FOLLY_HAS_COROUTINES

//  A place for foundational vocabulary types.
//
//  This header reexports the foundational vocabulary coroutine-helper types
//  from the standard, and exports several new foundational vocabulary types
//  as well.
//
//  Types which are non-foundational and non-vocabulary should go elsewhere.

#if FOLLY_HAS_COROUTINES

namespace folly {
class exception_wrapper;
struct AsyncStackFrame;
} // namespace folly

namespace folly::coro {

#if FOLLY_USE_STD_COROUTINE
namespace impl = std;
#else
namespace impl = std::experimental;
#endif

using impl::coroutine_handle;
using impl::coroutine_traits;
using impl::noop_coroutine;
using impl::noop_coroutine_handle;
using impl::noop_coroutine_promise;
using impl::suspend_always;
using impl::suspend_never;

//  ready_awaitable
//
//  An awaitable which is immediately ready with a value. Suspension is no-op.
//  Resumption returns the value.
//
//  The value type is permitted to be a reference.
template <typename T = void>
class ready_awaitable {
  static_assert(!std::is_void<T>::value, "base template unsuitable for void");

 public:
  explicit ready_awaitable(T value) //
      noexcept(noexcept(T(FOLLY_DECLVAL(T&&))))
      : value_(static_cast<T&&>(value)) {}

  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  T await_resume() noexcept(noexcept(T(FOLLY_DECLVAL(T&&)))) {
    return static_cast<T&&>(value_);
  }

 private:
  T value_;
};

//  ready_awaitable
//
//  An awaitable type which is immediately ready. Suspension is a no-op.
template <>
class ready_awaitable<void> {
 public:
  ready_awaitable() noexcept = default;

  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

namespace detail {

//  await_suspend_return_coroutine_fn
//  await_suspend_return_coroutine
//
//  The special member await_suspend has three forms, differing in their return
//  types. It may return void, bool, or coroutine_handle<>. This invokes member
//  await_suspend on the argument, conspiring always to return coroutine_handle
//  no matter the underlying form of member await_suspend on the argument.
struct await_suspend_return_coroutine_fn {
  template <typename A, typename P>
  coroutine_handle<> operator()(A& a, coroutine_handle<P> coro) const
      noexcept(noexcept(a.await_suspend(coro))) {
    using result = decltype(a.await_suspend(coro));
    if constexpr (std::is_same<void, result>::value) {
      a.await_suspend(coro);
      return noop_coroutine();
    } else if constexpr (std::is_same<bool, result>::value) {
      return a.await_suspend(coro) ? noop_coroutine() : coro;
    } else {
      return a.await_suspend(coro);
    }
  }
};
inline constexpr await_suspend_return_coroutine_fn
    await_suspend_return_coroutine{};

} // namespace detail

#if __has_include(<variant>)

//  variant_awaitable
//
//  An awaitable type which is backed by one of several possible underlying
//  awaitables.
template <typename... A>
class variant_awaitable : private std::variant<A...> {
 private:
  using base = std::variant<A...>;

  template <typename Visitor>
  auto visit(Visitor v) {
    return std::visit(v, static_cast<base&>(*this));
  }

 public:
  // imports the base-class constructors wholesale for implementation simplicity
  using base::base; // assume there are no valueless-by-exception instances

  auto await_ready() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_ready()) && ...)) {
    return visit([&](auto& a) { return a.await_ready(); });
  }
  template <typename P>
  auto await_suspend(coroutine_handle<P> coro) noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_suspend(coro)) && ...)) {
    auto impl = detail::await_suspend_return_coroutine;
    return visit([&](auto& a) { return impl(a, coro); });
  }
  auto await_resume() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_resume()) && ...)) {
    return visit([&](auto& a) { return a.await_resume(); });
  }
};

#endif // __has_include(<variant>)

//  ----

namespace detail {

struct detect_promise_return_object_eager_conversion_ {
  struct promise_type {
    struct return_object {
      /* implicit */ return_object(promise_type& p) noexcept : promise{&p} {
        promise->object = this;
      }
      ~return_object() {
        if (promise) {
          promise->object = nullptr;
        }
      }

      promise_type* promise;
    };

    ~promise_type() {
      if (object) {
        object->promise = nullptr;
      }
    }

    suspend_never initial_suspend() const noexcept { return {}; }
    suspend_never final_suspend() const noexcept { return {}; }
    void unhandled_exception() {}

    return_object get_return_object() noexcept { return {*this}; }
    void return_void() {}

    return_object* object = nullptr;
  };

  /* implicit */ detect_promise_return_object_eager_conversion_(
      promise_type::return_object const& o) noexcept
      : eager{!!o.promise} {}
  //  letting the coroutine type be trivially-copyable makes the coroutine crash
  //  under clang; to work around, provide an empty but not trivial destructor
  ~detect_promise_return_object_eager_conversion_() {}

  bool eager = false;

// FIXME: when building against Apple SDKs using c++17, we hit this all over
// the place on complex testing infrastructure for iOS. Since it's not clear
// how to fix the issue properly right now, force ignore this warnings and help
// unblock expected/optional coroutines. This should be removed once the build
// configuration is changed to use -Wno-deprecated-experimental-coroutine.
#if defined(__clang__) && (__clang_major__ < 17 && __clang_major__ > 13)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-experimental-coroutine"
  static detect_promise_return_object_eager_conversion_ go() noexcept {
    co_return;
  }
#pragma clang diagnostic pop
#else
  static detect_promise_return_object_eager_conversion_ go() noexcept {
    co_return;
  }
#endif
};

} // namespace detail

//  detect_promise_return_object_eager_conversion
//
//  Returns true if the compiler implements coroutine promise return-object
//  conversion eagerly and returns false if the compiler defers conversion.
//
//  It is expected that the caller holds the promise return-object until the
//  promise is fulfilled, even when it is not the same type as the coroutine.
//
//    auto ret = promise.get_return_object();
//    initial-suspend, etc...
//    return ret;
//
//  But this expected behavior was, mistakenly, never specified.
//
//  Some compilers misbehave, where the caller holds precisely the coroutine
//  type by converting the promise return-object eagerly when it is of some
//  type different from the coroutine type.
//
//    coro-type ret = promise.get_return_object();
//    initial-suspend, etc...
//    return ret;
//
//  Known behaviors are as follows:
//  * For msvc, conversion is eager for vs < 2019 update 16.5 (msc ver 1925) and
//    is deferred for vs >= 2019 update 16.5 (msc ver 1925).
//    References:
//      https://developercommunity.visualstudio.com/t/c-coroutine-get-return-object-converted-too-early/222420
//  * For g++, conversion is deferred.
//  * For clang++, conversion is eager for 15 <= llvm < 17 and is deferred for
//    llvm < 15 or llvm >= 17.
//    References:
//      https://reviews.llvm.org/D117087
//      https://github.com/llvm/llvm-project/issues/56532
//      https://reviews.llvm.org/D145639
//
//  Meta sometimes uses llvm patched to have deferred conversion where the
//  corresponding upstream implements eager conversion. So version numbers do
//  not tell the whole story.
//
//  This function detects which behavior the compiler implements at a mix of
//  compile time and run time, depending on the compiler. It is only necessary
//  to do the runtime detection for llvm but, conveniently, llvm is able to do
//  full heap-allocation elision ("HALO") and optimize the detection down to a
//  constant.
//
//  TODO: Remove this detection once the behavior is specified.
inline bool detect_promise_return_object_eager_conversion() {
  using coro = detail::detect_promise_return_object_eager_conversion_;
  constexpr auto t = kMscVer && kMscVer < 1925;
  constexpr auto f = (kGnuc && !kIsClang) || (kMscVer >= 1925);
  return t ? true : f ? false : coro::go().eager;
}

class ExtendedCoroutineHandle;

// Extended promise interface folly::coro types are expected to implement
class ExtendedCoroutinePromise {
 public:
  virtual coroutine_handle<> getHandle() = 0;
  // Types may provide a more efficient resumption path when they know they will
  // be receiving an error result from the awaitee.
  // If they do, they might also update the active stack frame.
  virtual std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper&) = 0;

 protected:
  ~ExtendedCoroutinePromise() = default;
};

// Extended version of coroutine_handle<void>
// Assumes (and enforces) assumption that coroutine_handle is a pointer
class ExtendedCoroutineHandle {
 public:
  template <typename Promise>
  /*implicit*/ ExtendedCoroutineHandle(
      coroutine_handle<Promise> handle) noexcept
      : basic_(handle), extended_(fromBasic(handle)) {}

  /*implicit*/ ExtendedCoroutineHandle(coroutine_handle<> handle) noexcept
      : basic_(handle) {}

  /*implicit*/ ExtendedCoroutineHandle(ExtendedCoroutinePromise* ptr) noexcept
      : basic_(ptr->getHandle()), extended_(ptr) {}

  ExtendedCoroutineHandle() noexcept = default;

  void resume() { basic_.resume(); }

  void destroy() { basic_.destroy(); }

  coroutine_handle<> getHandle() const noexcept { return basic_; }

  ExtendedCoroutinePromise* getPromise() const noexcept { return extended_; }

  std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper& ex) {
    if (extended_) {
      return extended_->getErrorHandle(ex);
    }
    return {basic_, nullptr};
  }

  explicit operator bool() const noexcept { return !!basic_; }

 private:
  template <typename Promise>
  static auto fromBasic(coroutine_handle<Promise> handle) noexcept {
    if constexpr (std::is_convertible_v<Promise*, ExtendedCoroutinePromise*>) {
      return static_cast<ExtendedCoroutinePromise*>(&handle.promise());
    } else {
      return nullptr;
    }
  }

  coroutine_handle<> basic_;
  ExtendedCoroutinePromise* extended_{nullptr};
};

template <typename Promise>
class ExtendedCoroutinePromiseImpl : public ExtendedCoroutinePromise {
 public:
  coroutine_handle<> getHandle() final {
    return coroutine_handle<Promise>::from_promise(
        *static_cast<Promise*>(this));
  }

  std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
      exception_wrapper&) override {
    return {getHandle(), nullptr};
  }

 protected:
  ~ExtendedCoroutinePromiseImpl() = default;
};

} // namespace folly::coro

#endif
