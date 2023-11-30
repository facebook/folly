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

#include <memory>

#include <folly/Executor.h>
#include <folly/Traits.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/WithAsyncStack.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/Malloc.h>
#include <folly/io/async/Request.h>
#include <folly/lang/CustomizationPoint.h>
#include <folly/tracing/AsyncStack.h>

#include <glog/logging.h>

#if FOLLY_HAS_COROUTINES

namespace folly {

namespace coro {

namespace detail {

class ViaCoroutinePromiseBase {
 public:
  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  suspend_always initial_suspend() noexcept { return {}; }

  void return_void() noexcept {}

  [[noreturn]] void unhandled_exception() noexcept {
    folly::assume_unreachable();
  }

  void setExecutor(folly::Executor::KeepAlive<> executor) noexcept {
    executor_ = std::move(executor);
  }

  void setContinuation(ExtendedCoroutineHandle continuation) noexcept {
    continuation_ = continuation;
  }

  void setAsyncFrame(folly::AsyncStackFrame& frame) noexcept {
    asyncFrame_ = &frame;
  }

  void setRequestContext(
      std::shared_ptr<folly::RequestContext> context) noexcept {
    context_ = std::move(context);
  }

 protected:
  void scheduleContinuation() noexcept {
    // Pass the coroutine's RequestContext to Executor::add(), in case the
    // Executor implementation wants to know what runs on it (e.g. for stats).
    RequestContextScopeGuard contextScope{context_};

    executor_->add([this]() noexcept { this->executeContinuation(); });
  }

 private:
  void executeContinuation() noexcept {
    RequestContextScopeGuard contextScope{std::move(context_)};
    if (asyncFrame_ != nullptr) {
      folly::resumeCoroutineWithNewAsyncStackRoot(
          continuation_.getHandle(), *asyncFrame_);
    } else {
      continuation_.resume();
    }
  }

 protected:
  virtual ~ViaCoroutinePromiseBase() = default;

  folly::Executor::KeepAlive<> executor_;
  ExtendedCoroutineHandle continuation_;
  folly::AsyncStackFrame* asyncFrame_ = nullptr;
  std::shared_ptr<RequestContext> context_;
};

template <bool IsStackAware>
class ViaCoroutine {
 public:
  class promise_type final : public ViaCoroutinePromiseBase,
                             public ExtendedCoroutinePromiseImpl<promise_type> {
    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }

      // This code runs immediately after the inner awaitable resumes its fake
      // continuation, and it schedules the real continuation on the awaiter's
      // executor
      FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES void await_suspend(
          coroutine_handle<promise_type> h) noexcept {
        auto& promise = h.promise();
        if (!promise.context_) {
          promise.setRequestContext(RequestContext::saveContext());
        }

        if constexpr (IsStackAware) {
          folly::deactivateAsyncStackFrame(promise.getAsyncFrame());
        }

        promise.scheduleContinuation();
      }

      [[noreturn]] void await_resume() noexcept { folly::assume_unreachable(); }
    };

   public:
    ViaCoroutine get_return_object() noexcept {
      return ViaCoroutine{coroutine_handle<promise_type>::from_promise(*this)};
    }

    FinalAwaiter final_suspend() noexcept { return {}; }

    template <
        bool IsStackAware2 = IsStackAware,
        std::enable_if_t<IsStackAware2, int> = 0>
    folly::AsyncStackFrame& getAsyncFrame() noexcept {
      DCHECK(this->asyncFrame_ != nullptr);
      return *this->asyncFrame_;
    }

    std::pair<ExtendedCoroutineHandle, AsyncStackFrame*> getErrorHandle(
        exception_wrapper& ex) override {
      auto [handle, frame] = continuation_.getErrorHandle(ex);
      setContinuation(handle);
      if (frame && IsStackAware) {
        asyncFrame_ = frame;
      }
      return {promise_type::getHandle(), nullptr};
    }
  };

  ViaCoroutine(ViaCoroutine&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~ViaCoroutine() {
    if (coro_) {
      coro_.destroy();
    }
  }

  static ViaCoroutine create(folly::Executor::KeepAlive<> executor) {
    ViaCoroutine coroutine = createImpl();
    coroutine.setExecutor(std::move(executor));
    return coroutine;
  }

  void setExecutor(folly::Executor::KeepAlive<> executor) noexcept {
    coro_.promise().setExecutor(std::move(executor));
  }

  void setContinuation(ExtendedCoroutineHandle continuation) noexcept {
    coro_.promise().setContinuation(continuation);
  }

  void setAsyncFrame(folly::AsyncStackFrame& frame) noexcept {
    coro_.promise().setAsyncFrame(frame);
  }

  void destroy() noexcept {
    if (coro_) {
      std::exchange(coro_, {}).destroy();
    }
  }

  void saveContext() noexcept {
    coro_.promise().setRequestContext(folly::RequestContext::saveContext());
  }

  coroutine_handle<promise_type> getHandle() noexcept { return coro_; }

 private:
  explicit ViaCoroutine(coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  static ViaCoroutine createImpl() { co_return; }

  coroutine_handle<promise_type> coro_;
};

} // namespace detail

template <typename Awaitable>
class StackAwareViaIfAsyncAwaiter {
  using WithAsyncStackAwaitable =
      decltype(folly::coro::co_withAsyncStack(std::declval<Awaitable>()));
  using Awaiter = folly::coro::awaiter_type_t<WithAsyncStackAwaitable>;
  using CoroutineType = detail::ViaCoroutine<true>;
  using CoroutinePromise = typename CoroutineType::promise_type;
  using WrapperHandle = coroutine_handle<CoroutinePromise>;

  using await_suspend_result_t =
      decltype(std::declval<Awaiter&>().await_suspend(
          std::declval<WrapperHandle>()));

 public:
  explicit StackAwareViaIfAsyncAwaiter(
      folly::Executor::KeepAlive<> executor, Awaitable&& awaitable)
      : viaCoroutine_(CoroutineType::create(std::move(executor))),
        awaitable_(folly::coro::co_withAsyncStack(
            static_cast<Awaitable&&>(awaitable))),
        awaiter_(folly::coro::get_awaiter(
            static_cast<WithAsyncStackAwaitable&&>(awaitable_))) {}

  decltype(auto) await_ready() noexcept(noexcept(awaiter_.await_ready())) {
    return awaiter_.await_ready();
  }

  template <typename Promise>
  auto await_suspend(coroutine_handle<Promise> h) noexcept(noexcept(
      std::declval<Awaiter&>().await_suspend(std::declval<WrapperHandle>())))
      -> await_suspend_result_t {
    auto& promise = h.promise();
    auto& asyncFrame = promise.getAsyncFrame();

    viaCoroutine_.setContinuation(h);
    viaCoroutine_.setAsyncFrame(asyncFrame);

    if constexpr (!detail::is_coroutine_handle_v<await_suspend_result_t>) {
      viaCoroutine_.saveContext();
    }

    return awaiter_.await_suspend(viaCoroutine_.getHandle());
  }

  decltype(auto) await_resume() noexcept(noexcept(awaiter_.await_resume())) {
    viaCoroutine_.destroy();
    return awaiter_.await_resume();
  }

  template <
      typename Awaiter2 = Awaiter,
      typename Result = decltype(std::declval<Awaiter2&>().await_resume_try())>
  Result await_resume_try() noexcept(
      noexcept(std::declval<Awaiter&>().await_resume_try())) {
    viaCoroutine_.destroy();
    return awaiter_.await_resume_try();
  }

 private:
  CoroutineType viaCoroutine_;
  WithAsyncStackAwaitable awaitable_;
  Awaiter awaiter_;
};

template <bool IsCallerAsyncStackAware, typename Awaitable>
class ViaIfAsyncAwaiter {
  using Awaiter = folly::coro::awaiter_type_t<Awaitable>;
  using CoroutineType = detail::ViaCoroutine<false>;
  using CoroutinePromise = typename CoroutineType::promise_type;
  using WrapperHandle = coroutine_handle<CoroutinePromise>;

  using await_suspend_result_t =
      decltype(std::declval<Awaiter&>().await_suspend(
          std::declval<WrapperHandle>()));

 public:
  explicit ViaIfAsyncAwaiter(
      folly::Executor::KeepAlive<> executor, Awaitable&& awaitable)
      : viaCoroutine_(CoroutineType::create(std::move(executor))),
        awaiter_(
            folly::coro::get_awaiter(static_cast<Awaitable&&>(awaitable))) {}

  decltype(auto) await_ready() noexcept(noexcept(awaiter_.await_ready())) {
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

  template <typename Promise>
  auto await_suspend(coroutine_handle<Promise> continuation) noexcept(noexcept(
      std::declval<Awaiter&>().await_suspend(std::declval<WrapperHandle>())))
      -> await_suspend_result_t {
    viaCoroutine_.setContinuation(continuation);

    if constexpr (!detail::is_coroutine_handle_v<await_suspend_result_t>) {
      viaCoroutine_.saveContext();
    }

    if constexpr (IsCallerAsyncStackAware) {
      auto& asyncFrame = continuation.promise().getAsyncFrame();
      auto& stackRoot = *asyncFrame.getStackRoot();

      viaCoroutine_.setAsyncFrame(asyncFrame);

      folly::deactivateAsyncStackFrame(asyncFrame);

      // Reactivate the stack-frame before we resume.
      auto rollback =
          makeGuard([&] { activateAsyncStackFrame(stackRoot, asyncFrame); });
      if constexpr (std::is_same_v<await_suspend_result_t, bool>) {
        if (!awaiter_.await_suspend(viaCoroutine_.getHandle())) {
          return false;
        }
        rollback.dismiss();
        return true;
      } else if constexpr (std::is_same_v<await_suspend_result_t, void>) {
        awaiter_.await_suspend(viaCoroutine_.getHandle());
        rollback.dismiss();
        return;
      } else {
        auto ret = awaiter_.await_suspend(viaCoroutine_.getHandle());
        rollback.dismiss();
        return ret;
      }
    } else {
      return awaiter_.await_suspend(viaCoroutine_.getHandle());
    }
  }

  auto await_resume() noexcept(
      noexcept(std::declval<Awaiter&>().await_resume()))
      -> decltype(std::declval<Awaiter&>().await_resume()) {
    viaCoroutine_.destroy();
    return awaiter_.await_resume();
  }

  template <
      typename Awaiter2 = Awaiter,
      typename Result = decltype(std::declval<Awaiter2&>().await_resume_try())>
  Result await_resume_try() noexcept(
      noexcept(std::declval<Awaiter&>().await_resume_try())) {
    viaCoroutine_.destroy();
    return awaiter_.await_resume_try();
  }

 private:
  CoroutineType viaCoroutine_;
  Awaiter awaiter_;
};

template <typename Awaitable>
class StackAwareViaIfAsyncAwaitable {
 public:
  explicit StackAwareViaIfAsyncAwaitable(
      folly::Executor::KeepAlive<> executor,
      Awaitable&&
          awaitable) noexcept(std::is_nothrow_move_constructible<Awaitable>::
                                  value)
      : executor_(std::move(executor)),
        awaitable_(static_cast<Awaitable&&>(awaitable)) {}

  auto operator co_await() && {
    if constexpr (is_awaitable_async_stack_aware_v<Awaitable>) {
      return StackAwareViaIfAsyncAwaiter<Awaitable>{
          std::move(executor_), static_cast<Awaitable&&>(awaitable_)};
    } else {
      return ViaIfAsyncAwaiter<true, Awaitable>{
          std::move(executor_), static_cast<Awaitable&&>(awaitable_)};
    }
  }

 private:
  folly::Executor::KeepAlive<> executor_;
  Awaitable awaitable_;
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

  ViaIfAsyncAwaiter<false, Awaitable> operator co_await() && {
    return ViaIfAsyncAwaiter<false, Awaitable>{
        std::move(executor_), static_cast<Awaitable&&>(awaitable_)};
  }

  friend StackAwareViaIfAsyncAwaitable<Awaitable> tag_invoke(
      cpo_t<co_withAsyncStack>, ViaIfAsyncAwaitable&& self) {
    return StackAwareViaIfAsyncAwaitable<Awaitable>{
        std::move(self.executor_), static_cast<Awaitable&&>(self.awaitable_)};
  }

 private:
  folly::Executor::KeepAlive<> executor_;
  Awaitable awaitable_;
};

namespace detail {

template <typename SemiAwaitable, typename = void>
struct HasViaIfAsyncMethod : bool_constant<!sizeof(SemiAwaitable)> {};

template <typename SemiAwaitable>
struct HasViaIfAsyncMethod<
    SemiAwaitable,
    std::enable_if_t<std::is_void_v<SemiAwaitable>>> : std::false_type {};

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
  return ViaIfAsyncAwaitable<Awaitable>{
      std::move(executor), static_cast<Awaitable&&>(awaitable)};
}

struct ViaIfAsyncFunction {
  template <typename Awaitable>
  auto operator()(folly::Executor::KeepAlive<> executor, Awaitable&& awaitable)
      const noexcept(noexcept(co_viaIfAsync(
          std::move(executor), static_cast<Awaitable&&>(awaitable))))
          -> decltype(co_viaIfAsync(
              std::move(executor), static_cast<Awaitable&&>(awaitable))) {
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
struct is_semi_awaitable : bool_constant<!sizeof(T)> {};

template <typename T>
struct is_semi_awaitable<T, std::enable_if_t<std::is_void_v<T>>>
    : std::false_type {};

template <typename T>
struct is_semi_awaitable<
    T,
    void_t<decltype(folly::coro::co_viaIfAsync(
        std::declval<folly::Executor::KeepAlive<>>(), std::declval<T>()))>>
    : std::true_type {};

template <typename T>
constexpr bool is_semi_awaitable_v = is_semi_awaitable<T>::value;

template <typename T>
using semi_await_result_t = await_result_t<decltype(folly::coro::co_viaIfAsync(
    std::declval<folly::Executor::KeepAlive<>>(), std::declval<T>()))>;

namespace detail {

template <typename Awaitable>
class TryAwaiter {
  using Awaiter = awaiter_type_t<Awaitable>;

 public:
  explicit TryAwaiter(Awaitable&& awaiter)
      : awaiter_(folly::coro::get_awaiter(static_cast<Awaitable&&>(awaiter))) {}

  auto await_ready() noexcept(noexcept(std::declval<Awaiter&>().await_ready()))
      -> decltype(std::declval<Awaiter&>().await_ready()) {
    return awaiter_.await_ready();
  }

  template <typename Promise>
  auto await_suspend(coroutine_handle<Promise> coro) noexcept(
      noexcept(std::declval<Awaiter&>().await_suspend(coro)))
      -> decltype(std::declval<Awaiter&>().await_suspend(coro)) {
    return awaiter_.await_suspend(coro);
  }

  auto await_resume() noexcept(
      noexcept(std::declval<Awaiter&>().await_resume_try()))
      -> decltype(std::declval<Awaiter&>().await_resume_try()) {
    return awaiter_.await_resume_try();
  }

 private:
  Awaiter awaiter_;
};

/**
 * Common machinery for building wrappers like co_awaitTry
 * Allows the wrapper to commute with the universal wrappers like
 * co_withCancellation while keeping the corresponding awaitable on the outside
 */
template <template <typename T> typename Derived, typename T>
class CommutativeWrapperAwaitable {
 public:
  template <typename T2>
  explicit CommutativeWrapperAwaitable(T2&& awaitable) noexcept(
      std::is_nothrow_constructible_v<T, T2>)
      : inner_(static_cast<T2&&>(awaitable)) {}

  template <typename Factory>
  explicit CommutativeWrapperAwaitable(std::in_place_t, Factory&& factory)
      : inner_(factory()) {}

  template <
      typename T2 = T,
      typename Result = decltype(folly::coro::co_withCancellation(
          std::declval<const folly::CancellationToken&>(), std::declval<T2>()))>
  friend Derived<Result> co_withCancellation(
      const folly::CancellationToken& cancelToken, Derived<T>&& awaitable) {
    return Derived<Result>{std::in_place, [&]() -> decltype(auto) {
                             return folly::coro::co_withCancellation(
                                 cancelToken,
                                 static_cast<T&&>(awaitable.inner_));
                           }};
  }

  template <
      typename T2 = T,
      typename Result =
          decltype(folly::coro::co_withAsyncStack(std::declval<T2>()))>
  friend Derived<Result>
  tag_invoke(cpo_t<co_withAsyncStack>, Derived<T>&& awaitable) noexcept(
      noexcept(folly::coro::co_withAsyncStack(std::declval<T2>()))) {
    return Derived<Result>{std::in_place, [&]() -> decltype(auto) {
                             return folly::coro::co_withAsyncStack(
                                 static_cast<T&&>(awaitable.inner_));
                           }};
  }

  template <
      typename T2 = T,
      typename Result = decltype(folly::coro::co_viaIfAsync(
          std::declval<folly::Executor::KeepAlive<>>(), std::declval<T2>()))>
  friend Derived<Result> co_viaIfAsync(
      folly::Executor::KeepAlive<> executor,
      Derived<T>&&
          awaitable) noexcept(noexcept(folly::coro::
                                           co_viaIfAsync(
                                               std::declval<folly::Executor::
                                                                KeepAlive<>>(),
                                               std::declval<T2>()))) {
    return Derived<Result>{std::in_place, [&]() -> decltype(auto) {
                             return folly::coro::co_viaIfAsync(
                                 std::move(executor),
                                 static_cast<T&&>(awaitable.inner_));
                           }};
  }

 protected:
  T inner_;
};

template <typename T>
class TryAwaitable : public CommutativeWrapperAwaitable<TryAwaitable, T> {
 public:
  using CommutativeWrapperAwaitable<TryAwaitable, T>::
      CommutativeWrapperAwaitable;

  template <
      typename Self,
      std::enable_if_t<
          std::is_same_v<remove_cvref_t<Self>, TryAwaitable>,
          int> = 0,
      typename T2 = like_t<Self, T>,
      std::enable_if_t<is_awaitable_v<T2>, int> = 0>
  friend TryAwaiter<T2> operator co_await(Self&& self) {
    return TryAwaiter<T2>{static_cast<Self&&>(self).inner_};
  }
};

} // namespace detail

template <typename Awaitable>
detail::TryAwaitable<remove_cvref_t<Awaitable>> co_awaitTry(
    Awaitable&& awaitable) {
  return detail::TryAwaitable<remove_cvref_t<Awaitable>>{
      static_cast<Awaitable&&>(awaitable)};
}

template <typename T>
using semi_await_try_result_t =
    await_result_t<decltype(folly::coro::co_viaIfAsync(
        std::declval<folly::Executor::KeepAlive<>>(),
        folly::coro::co_awaitTry(std::declval<T>())))>;

namespace detail {

template <typename T>
class NothrowAwaitable
    : public CommutativeWrapperAwaitable<NothrowAwaitable, T> {
 public:
  using CommutativeWrapperAwaitable<NothrowAwaitable, T>::
      CommutativeWrapperAwaitable;

  T&& unwrap() { return std::move(this->inner_); }
};

} // namespace detail

template <typename Awaitable>
detail::NothrowAwaitable<remove_cvref_t<Awaitable>> co_nothrow(
    Awaitable&& awaitable) {
  return detail::NothrowAwaitable<remove_cvref_t<Awaitable>>{
      static_cast<Awaitable&&>(awaitable)};
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
