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

#include <experimental/coroutine>
#include <memory>

#include <folly/Executor.h>
#include <folly/Traits.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/io/async/Request.h>
#include <folly/lang/CustomizationPoint.h>

#include <glog/logging.h>

namespace folly {

class InlineExecutor;

namespace coro {

namespace detail {

class ViaCoroutine {
 public:
  class promise_type {
   public:
    // Passed as lvalue by compiler, but should have no other dependencies
    promise_type(folly::Executor::KeepAlive<>& executor) noexcept
        : executor_(std::move(executor)) {}

    static void* operator new(std::size_t size) {
      return ::folly_coro_async_malloc(size);
    }

    static void operator delete(void* ptr, std::size_t size) {
      ::folly_coro_async_free(ptr, size);
    }

    ViaCoroutine get_return_object() noexcept {
      return ViaCoroutine{
          std::experimental::coroutine_handle<promise_type>::from_promise(
              *this)};
    }

    std::experimental::suspend_always initial_suspend() noexcept {
      return {};
    }

    auto final_suspend() noexcept {
      struct Awaiter {
        bool await_ready() noexcept {
          return false;
        }
        FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend(
            std::experimental::coroutine_handle<promise_type> coro) noexcept {
          // Schedule resumption of the coroutine on the executor.
          auto& promise = coro.promise();
          if (!promise.context_) {
            promise.context_ = RequestContext::saveContext();
          }

          promise.executor_->add([&promise]() noexcept {
            RequestContextScopeGuard contextScope{std::move(promise.context_)};
            promise.continuation_.resume();
          });
        }
        void await_resume() noexcept {}
      };

      return Awaiter{};
    }

    [[noreturn]] void unhandled_exception() noexcept {
      LOG(FATAL) << "ViaCoroutine threw an unhandled exception";
    }

    void return_void() noexcept {}

    void setContinuation(
        std::experimental::coroutine_handle<> continuation) noexcept {
      DCHECK(!continuation_);
      continuation_ = continuation;
    }

    void setContext(std::shared_ptr<RequestContext> context) noexcept {
      context_ = std::move(context);
    }

   private:
    folly::Executor::KeepAlive<> executor_;
    std::experimental::coroutine_handle<> continuation_;
    std::shared_ptr<RequestContext> context_;
  };

  ViaCoroutine(ViaCoroutine&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~ViaCoroutine() {
    destroy();
  }

  ViaCoroutine& operator=(ViaCoroutine other) noexcept {
    swap(other);
    return *this;
  }

  void swap(ViaCoroutine& other) noexcept {
    std::swap(coro_, other.coro_);
  }

  std::experimental::coroutine_handle<> getWrappedCoroutine(
      std::experimental::coroutine_handle<> continuation) noexcept {
    if (coro_) {
      coro_.promise().setContinuation(continuation);
      return coro_;
    } else {
      return continuation;
    }
  }

  std::experimental::coroutine_handle<> getWrappedCoroutineWithSavedContext(
      std::experimental::coroutine_handle<> continuation) noexcept {
    coro_.promise().setContext(RequestContext::saveContext());
    return getWrappedCoroutine(continuation);
  }

  void destroy() {
    if (coro_) {
      std::exchange(coro_, {}).destroy();
    }
  }

  static ViaCoroutine create(folly::Executor::KeepAlive<> executor) {
    co_return;
  }

  static ViaCoroutine createInline() noexcept {
    return ViaCoroutine{std::experimental::coroutine_handle<promise_type>{}};
  }

 private:
  friend class promise_type;

  explicit ViaCoroutine(
      std::experimental::coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  std::experimental::coroutine_handle<promise_type> coro_;
};

} // namespace detail

template <typename Awaiter>
class ViaIfAsyncAwaiter {
  using await_suspend_result_t =
      decltype(std::declval<Awaiter&>().await_suspend(
          std::declval<std::experimental::coroutine_handle<>>()));

 public:
  static_assert(
      folly::coro::is_awaiter_v<Awaiter>,
      "Awaiter type does not implement the Awaiter interface.");

  template <typename Awaitable>
  explicit ViaIfAsyncAwaiter(
      folly::Executor::KeepAlive<> executor,
      Awaitable&& awaitable)
      : viaCoroutine_(detail::ViaCoroutine::create(std::move(executor))),
        awaiter_(
            folly::coro::get_awaiter(static_cast<Awaitable&&>(awaitable))) {}

  bool await_ready() noexcept(
      noexcept(std::declval<Awaiter&>().await_ready())) {
    DCHECK(true);
    return awaiter_.await_ready();
  }

  // NOTE: We are using a heuristic here to determine when is the correct
  // time to capture the RequestContext. We want to capture the context just
  // before the coroutine suspends and execution is returned to the executor.
  //
  // In cases where we are awaiting another coroutine and symmetrically
  // transferring execution to another coroutine we are not yet returning
  // execution to the executor so we want to defer capturing the context until
  // the ViaCoroutine is resumed and suspends in final_suspend() before
  // scheduling the resumption on the executor.
  //
  // In cases where the awaitable may suspend without transferring execution
  // to another coroutine and will therefore return back to the executor we
  // want to capture the execution context before calling into the wrapped
  // awaitable's await_suspend() method (since it's await_suspend() method
  // might schedule resumption on another thread and could resume and destroy
  // the ViaCoroutine before the await_suspend() method returns).
  //
  // The heuristic is that if await_suspend() returns a coroutine_handle
  // then we assume it's the first case. Otherwise if await_suspend() returns
  // void/bool then we assume it's the second case.
  //
  // This heuristic isn't perfect since a coroutine_handle-returning
  // await_suspend() method could return noop_coroutine() in which case we
  // could fail to capture the current context. Awaitable types that do this
  // would need to provide a custom implementation of co_viaIfAsync() that
  // correctly captures the RequestContext to get correct behaviour in this
  // case.

  template <
      typename Result = await_suspend_result_t,
      std::enable_if_t<
          folly::coro::detail::_is_coroutine_handle<Result>::value,
          int> = 0>
  auto
  await_suspend(std::experimental::coroutine_handle<> continuation) noexcept(
      noexcept(std::declval<Awaiter&>().await_suspend(continuation)))
      -> Result {
    return awaiter_.await_suspend(
        viaCoroutine_.getWrappedCoroutine(continuation));
  }

  template <
      typename Result = await_suspend_result_t,
      std::enable_if_t<
          !folly::coro::detail::_is_coroutine_handle<Result>::value,
          int> = 0>
  auto
  await_suspend(std::experimental::coroutine_handle<> continuation) noexcept(
      noexcept(std::declval<Awaiter&>().await_suspend(continuation)))
      -> Result {
    return awaiter_.await_suspend(
        viaCoroutine_.getWrappedCoroutineWithSavedContext(continuation));
  }

  decltype(auto) await_resume() noexcept(
      noexcept(std::declval<Awaiter&>().await_resume())) {
    viaCoroutine_.destroy();
    return awaiter_.await_resume();
  }

  detail::ViaCoroutine viaCoroutine_;
  Awaiter awaiter_;
};

template <typename Awaitable>
class ViaIfAsyncAwaitable {
 public:
  explicit ViaIfAsyncAwaitable(
      folly::Executor::KeepAlive<> executor,
      Awaitable&&
          awaitable) noexcept(std::is_nothrow_move_constructible<Awaitable>::
                                  value)
      : executor_(std::move(executor)),
        awaitable_(static_cast<Awaitable&&>(awaitable)) {}

  template <typename Awaitable2>
  friend auto operator co_await(ViaIfAsyncAwaitable<Awaitable2>&& awaitable)
      -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable2>>;

  template <typename Awaitable2>
  friend auto operator co_await(ViaIfAsyncAwaitable<Awaitable2>& awaitable)
      -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable2&>>;

  template <typename Awaitable2>
  friend auto operator co_await(
      const ViaIfAsyncAwaitable<Awaitable2>&& awaitable)
      -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable2&&>>;

  template <typename Awaitable2>
  friend auto operator co_await(
      const ViaIfAsyncAwaitable<Awaitable2>& awaitable)
      -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable2&>>;

 private:
  folly::Executor::KeepAlive<> executor_;
  Awaitable awaitable_;
};

template <typename Awaitable>
auto operator co_await(ViaIfAsyncAwaitable<Awaitable>&& awaitable)
    -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable>> {
  return ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable>>{
      std::move(awaitable.executor_),
      static_cast<Awaitable&&>(awaitable.awaitable_)};
}

template <typename Awaitable>
auto operator co_await(ViaIfAsyncAwaitable<Awaitable>& awaitable)
    -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable&>> {
  return ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<Awaitable&>>{
      awaitable.executor_, awaitable.awaitable_};
}

template <typename Awaitable>
auto operator co_await(const ViaIfAsyncAwaitable<Awaitable>&& awaitable)
    -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable&&>> {
  return ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable&&>>{
      std::move(awaitable.executor_),
      static_cast<const Awaitable&&>(awaitable.awaitable_)};
}

template <typename Awaitable>
auto operator co_await(const ViaIfAsyncAwaitable<Awaitable>& awaitable)
    -> ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable&>> {
  return ViaIfAsyncAwaiter<folly::coro::awaiter_type_t<const Awaitable&>>{
      awaitable.executor_, awaitable.awaitable_};
}

namespace detail {

template <typename SemiAwaitable, typename = void>
struct HasViaIfAsyncMethod : std::false_type {};

template <typename SemiAwaitable>
struct HasViaIfAsyncMethod<
    SemiAwaitable,
    void_t<decltype(std::declval<SemiAwaitable>().viaIfAsync(
        std::declval<folly::Executor::KeepAlive<>>()))>> : std::true_type {};

namespace adl {

template <typename SemiAwaitable>
auto co_viaIfAsync(
    folly::Executor::KeepAlive<> executor,
    SemiAwaitable&&
        awaitable) noexcept(noexcept(static_cast<SemiAwaitable&&>(awaitable)
                                         .viaIfAsync(std::move(executor))))
    -> decltype(static_cast<SemiAwaitable&&>(awaitable).viaIfAsync(
        std::move(executor))) {
  return static_cast<SemiAwaitable&&>(awaitable).viaIfAsync(
      std::move(executor));
}

template <
    typename Awaitable,
    std::enable_if_t<
        is_awaitable_v<Awaitable> && !HasViaIfAsyncMethod<Awaitable>::value,
        int> = 0>
auto co_viaIfAsync(folly::Executor::KeepAlive<> executor, Awaitable&& awaitable)
    -> ViaIfAsyncAwaitable<Awaitable> {
  static_assert(
      folly::coro::is_awaitable_v<Awaitable>,
      "co_viaIfAsync() argument 2 is not awaitable.");
  return ViaIfAsyncAwaitable<Awaitable>{std::move(executor),
                                        static_cast<Awaitable&&>(awaitable)};
}

struct ViaIfAsyncFunction {
  template <typename Awaitable>
  auto operator()(folly::Executor::KeepAlive<> executor, Awaitable&& awaitable)
      const noexcept(noexcept(co_viaIfAsync(
          std::move(executor),
          static_cast<Awaitable&&>(awaitable))))
          -> decltype(co_viaIfAsync(
              std::move(executor),
              static_cast<Awaitable&&>(awaitable))) {
    return co_viaIfAsync(
        std::move(executor), static_cast<Awaitable&&>(awaitable));
  }
};

} // namespace adl
} // namespace detail

/// Returns a new awaitable that will resume execution of the awaiting coroutine
/// on a specified executor in the case that the operation does not complete
/// synchronously.
///
/// If the operation completes synchronously then the awaiting coroutine
/// will continue execution on the current thread without transitioning
/// execution to the specified executor.
FOLLY_DEFINE_CPO(detail::adl::ViaIfAsyncFunction, co_viaIfAsync)

template <typename T, typename = void>
struct is_semi_awaitable : std::false_type {};

template <typename T>
struct is_semi_awaitable<
    T,
    void_t<decltype(folly::coro::co_viaIfAsync(
        std::declval<folly::Executor::KeepAlive<>>(),
        std::declval<T>()))>> : std::true_type {};

template <typename T>
constexpr bool is_semi_awaitable_v = is_semi_awaitable<T>::value;

template <typename T>
using semi_await_result_t = await_result_t<decltype(folly::coro::co_viaIfAsync(
    std::declval<folly::Executor::KeepAlive<>>(),
    std::declval<T>()))>;

namespace detail {

template <typename Awaiter>
class TryAwaiter {
 public:
  TryAwaiter(Awaiter&& awaiter) : awaiter_(std::move(awaiter)) {}

  bool await_ready() {
    return awaiter_.await_ready();
  }

  template <typename Promise>
  auto await_suspend(std::experimental::coroutine_handle<Promise> coro) {
    return awaiter_.await_suspend(coro);
  }

  auto await_resume() {
    return awaiter_.await_resume_try();
  }

 private:
  Awaiter awaiter_;
};

template <typename Awaiter>
auto makeTryAwaiter(Awaiter&& awaiter) {
  return TryAwaiter<std::decay_t<Awaiter>>(std::move(awaiter));
}

template <typename SemiAwaitable>
class TrySemiAwaitable {
 public:
  explicit TrySemiAwaitable(SemiAwaitable&& semiAwaitable)
      : semiAwaitable_(std::move(semiAwaitable)) {}

  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor,
      TrySemiAwaitable&& self) noexcept {
    return makeTryAwaiter(get_awaiter(
        co_viaIfAsync(std::move(executor), std::move(self.semiAwaitable_))));
  }

  friend auto co_withCancellation(
      const CancellationToken& cancelToken,
      TrySemiAwaitable&& awaitable) {
    auto cancelAwaitable = folly::coro::co_withCancellation(
        std::move(cancelToken),
        static_cast<SemiAwaitable&&>(awaitable.semiAwaitable_));
    return TrySemiAwaitable<decltype(cancelAwaitable)>(
        std::move(cancelAwaitable));
  }

 private:
  SemiAwaitable semiAwaitable_;
};
} // namespace detail

template <
    typename SemiAwaitable,
    typename = std::enable_if_t<is_semi_awaitable_v<SemiAwaitable>>>
auto co_awaitTry(SemiAwaitable&& semiAwaitable) {
  return detail::TrySemiAwaitable<SemiAwaitable>(std::move(semiAwaitable));
}

} // namespace coro
} // namespace folly
