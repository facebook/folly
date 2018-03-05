/*
 * Copyright 2014-present Facebook, Inc.
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

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <folly/Executor.h>
#include <folly/Function.h>
#include <folly/Optional.h>
#include <folly/ScopeGuard.h>
#include <folly/Try.h>
#include <folly/Utility.h>
#include <folly/futures/FutureException.h>
#include <folly/futures/detail/FSM.h>
#include <folly/lang/Exception.h>
#include <folly/synchronization/MicroSpinLock.h>

#include <folly/io/async/Request.h>

namespace folly {
namespace futures {
namespace detail {

/*
        OnlyCallback
       /            \
  Start              Armed - Done
       \            /
         OnlyResult

This state machine is fairly self-explanatory. The most important bit is
that the callback is only executed on the transition from Armed to Done,
and that transition can happen immediately after transitioning from Only*
to Armed, if it is active (the usual case).
*/
enum class State : uint8_t {
  Start,
  OnlyResult,
  OnlyCallback,
  Armed,
  Done,
};

/// The shared state object for Future and Promise.
/// Some methods must only be called by either the Future thread or the
/// Promise thread. The Future thread is the thread that currently "owns" the
/// Future and its callback-related operations, and the Promise thread is
/// likewise the thread that currently "owns" the Promise and its
/// result-related operations. Also, Futures own interruption, Promises own
/// interrupt handlers. Unfortunately, there are things that users can do to
/// break this, and we can't detect that. However if they follow move
/// semantics religiously wrt threading, they should be ok.
///
/// It's worth pointing out that Futures and/or Promises can and usually will
/// migrate between threads, though this usually happens within the API code.
/// For example, an async operation will probably make a Promise, grab its
/// Future, then move the Promise into another thread that will eventually
/// fulfill it. With executors and via, this gets slightly more complicated at
/// first blush, but it's the same principle. In general, as long as the user
/// doesn't access a Future or Promise object from more than one thread at a
/// time there won't be any problems.
template <typename T>
class Core final {
  static_assert(!std::is_void<T>::value,
                "void futures are not supported. Use Unit instead.");
 public:
  /// This must be heap-constructed. There's probably a way to enforce that in
  /// code but since this is just internal detail code and I don't know how
  /// off-hand, I'm punting.
  Core() : result_(), fsm_(State::Start), attached_(2) {}

  explicit Core(Try<T>&& t)
    : result_(std::move(t)),
      fsm_(State::OnlyResult),
      attached_(1) {}

  template <typename... Args>
  explicit Core(in_place_t, Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args&&...>::value)
      : result_(in_place, in_place, std::forward<Args>(args)...),
        fsm_(State::OnlyResult),
        attached_(1) {}

  ~Core() {
    DCHECK(attached_ == 0);
  }

  // not copyable
  Core(Core const&) = delete;
  Core& operator=(Core const&) = delete;

  // not movable (see comment in the implementation of Future::then)
  Core(Core&&) noexcept = delete;
  Core& operator=(Core&&) = delete;

  /// May call from any thread
  bool hasResult() const noexcept {
    switch (fsm_.getState()) {
      case State::OnlyResult:
      case State::Armed:
      case State::Done:
        assert(!!result_);
        return true;

      default:
        return false;
    }
  }

  /// May call from any thread
  bool ready() const noexcept {
    return hasResult();
  }

  /// May call from any thread
  Try<T>& getTry() {
    if (ready()) {
      return *result_;
    } else {
      throwFutureNotReady();
    }
  }

  /// Call only from Future thread.
  template <typename F>
  void setCallback(F&& func) {
    bool transitionToArmed = false;
    auto setCallback_ = [&]{
      context_ = RequestContext::saveContext();
      callback_ = std::forward<F>(func);
    };

    FSM_START(fsm_)
      case State::Start:
        FSM_UPDATE(fsm_, State::OnlyCallback, setCallback_);
        break;

      case State::OnlyResult:
        FSM_UPDATE(fsm_, State::Armed, setCallback_);
        transitionToArmed = true;
        break;

      case State::OnlyCallback:
      case State::Armed:
      case State::Done:
        throw_exception<std::logic_error>("setCallback called twice");
    FSM_END

    // we could always call this, it is an optimization to only call it when
    // it might be needed.
    if (transitionToArmed) {
      maybeCallback();
    }
  }

  /// Call only from Promise thread
  void setResult(Try<T>&& t) {
    bool transitionToArmed = false;
    auto setResult_ = [&]{ result_ = std::move(t); };
    FSM_START(fsm_)
      case State::Start:
        FSM_UPDATE(fsm_, State::OnlyResult, setResult_);
        break;

      case State::OnlyCallback:
        FSM_UPDATE(fsm_, State::Armed, setResult_);
        transitionToArmed = true;
        break;

      case State::OnlyResult:
      case State::Armed:
      case State::Done:
      throw_exception<std::logic_error>("setResult called twice");
    FSM_END

    if (transitionToArmed) {
      maybeCallback();
    }
  }

  /// Called by a destructing Future (in the Future thread, by definition)
  void detachFuture() {
    activate();
    detachOne();
  }

  /// Called by a destructing Promise (in the Promise thread, by definition)
  void detachPromise() {
    // detachPromise() and setResult() should never be called in parallel
    // so we don't need to protect this.
    if (UNLIKELY(!result_)) {
      setResult(Try<T>(exception_wrapper(BrokenPromise(typeid(T).name()))));
    }
    detachOne();
  }

  /// May call from any thread
  void deactivate() {
    active_.store(false, std::memory_order_release);
  }

  /// May call from any thread
  void activate() {
    active_.store(true, std::memory_order_release);
    maybeCallback();
  }

  /// May call from any thread
  bool isActive() { return active_.load(std::memory_order_acquire); }

  /// Call only from Future thread, either before attaching a callback or after
  /// the callback has already been invoked, but not concurrently with anything
  /// which might trigger invocation of the callback
  void setExecutor(Executor* x, int8_t priority = Executor::MID_PRI) {
    auto s = fsm_.getState();
    DCHECK(s == State::Start || s == State::OnlyResult || s == State::Done);
    executor_ = x;
    priority_ = priority;
  }

  Executor* getExecutor() {
    return executor_;
  }

  /// Call only from Future thread
  void raise(exception_wrapper e) {
    if (!interruptLock_.try_lock()) {
      interruptLock_.lock();
    }
    if (!interrupt_ && !hasResult()) {
      interrupt_ = std::make_unique<exception_wrapper>(std::move(e));
      if (interruptHandler_) {
        interruptHandler_(*interrupt_);
      }
    }
    interruptLock_.unlock();
  }

  std::function<void(exception_wrapper const&)> getInterruptHandler() {
    if (!interruptHandlerSet_.load(std::memory_order_acquire)) {
      return nullptr;
    }
    if (!interruptLock_.try_lock()) {
      interruptLock_.lock();
    }
    auto handler = interruptHandler_;
    interruptLock_.unlock();
    return handler;
  }

  /// Call only from Promise thread
  void setInterruptHandler(std::function<void(exception_wrapper const&)> fn) {
    if (!interruptLock_.try_lock()) {
      interruptLock_.lock();
    }
    if (!hasResult()) {
      if (interrupt_) {
        fn(*interrupt_);
      } else {
        setInterruptHandlerNoLock(std::move(fn));
      }
    }
    interruptLock_.unlock();
  }

  void setInterruptHandlerNoLock(
      std::function<void(exception_wrapper const&)> fn) {
    interruptHandlerSet_.store(true, std::memory_order_relaxed);
    interruptHandler_ = std::move(fn);
  }

 private:
  // Helper class that stores a pointer to the `Core` object and calls
  // `derefCallback` and `detachOne` in the destructor.
  class CoreAndCallbackReference {
   public:
    explicit CoreAndCallbackReference(Core* core) noexcept : core_(core) {}

    ~CoreAndCallbackReference() {
      if (core_) {
        core_->derefCallback();
        core_->detachOne();
      }
    }

    CoreAndCallbackReference(CoreAndCallbackReference const& o) = delete;
    CoreAndCallbackReference& operator=(CoreAndCallbackReference const& o) =
        delete;

    CoreAndCallbackReference(CoreAndCallbackReference&& o) noexcept {
      std::swap(core_, o.core_);
    }

    Core* getCore() const noexcept {
      return core_;
    }

   private:
    Core* core_{nullptr};
  };

  void maybeCallback() {
    FSM_START(fsm_)
      case State::Armed:
        if (active_.load(std::memory_order_acquire)) {
          FSM_UPDATE2(fsm_, State::Done, []{}, [this]{ this->doCallback(); });
        }
        FSM_BREAK

      default:
        FSM_BREAK
    FSM_END
  }

  void doCallback() {
    Executor* x = executor_;
    int8_t priority = priority_;

    if (x) {
      exception_wrapper ew;
      // We need to reset `callback_` after it was executed (which can happen
      // through the executor or, if `Executor::add` throws, below). The
      // executor might discard the function without executing it (now or
      // later), in which case `callback_` also needs to be reset.
      // The `Core` has to be kept alive throughout that time, too. Hence we
      // increment `attached_` and `callbackReferences_` by two, and construct
      // exactly two `CoreAndCallbackReference` objects, which call
      // `derefCallback` and `detachOne` in their destructor. One will guard
      // this scope, the other one will guard the lambda passed to the executor.
      attached_ += 2;
      callbackReferences_ += 2;
      CoreAndCallbackReference guard_local_scope(this);
      CoreAndCallbackReference guard_lambda(this);
      try {
        if (LIKELY(x->getNumPriorities() == 1)) {
          x->add([core_ref = std::move(guard_lambda)]() mutable {
            auto cr = std::move(core_ref);
            Core* const core = cr.getCore();
            RequestContextScopeGuard rctx(core->context_);
            core->callback_(std::move(*core->result_));
          });
        } else {
          x->addWithPriority(
              [core_ref = std::move(guard_lambda)]() mutable {
                auto cr = std::move(core_ref);
                Core* const core = cr.getCore();
                RequestContextScopeGuard rctx(core->context_);
                core->callback_(std::move(*core->result_));
              },
              priority);
        }
      } catch (const std::exception& e) {
        ew = exception_wrapper(std::current_exception(), e);
      } catch (...) {
        ew = exception_wrapper(std::current_exception());
      }
      if (ew) {
        RequestContextScopeGuard rctx(context_);
        result_ = Try<T>(std::move(ew));
        callback_(std::move(*result_));
      }
    } else {
      attached_++;
      SCOPE_EXIT {
        callback_ = {};
        detachOne();
      };
      RequestContextScopeGuard rctx(context_);
      callback_(std::move(*result_));
    }
  }

  void detachOne() {
    auto a = attached_--;
    assert(a >= 1);
    if (a == 1) {
      delete this;
    }
  }

  void derefCallback() {
    if (--callbackReferences_ == 0) {
      callback_ = {};
    }
  }

  folly::Function<void(Try<T>&&)> callback_;
  // place result_ next to increase the likelihood that the value will be
  // contained entirely in one cache line
  folly::Optional<Try<T>> result_;
  FSM<State> fsm_;
  std::atomic<unsigned char> attached_;
  std::atomic<unsigned char> callbackReferences_{0};
  std::atomic<bool> active_ {true};
  std::atomic<bool> interruptHandlerSet_ {false};
  folly::MicroSpinLock interruptLock_ {0};
  int8_t priority_ {-1};
  Executor* executor_ {nullptr};
  std::shared_ptr<RequestContext> context_ {nullptr};
  std::unique_ptr<exception_wrapper> interrupt_ {};
  std::function<void(exception_wrapper const&)> interruptHandler_ {nullptr};
};

template <template <typename...> class T, typename... Ts>
void collectVariadicHelper(const std::shared_ptr<T<Ts...>>& /* ctx */) {
  // base case
}

template <
    template <typename...> class T,
    typename... Ts,
    typename THead,
    typename... TTail>
void collectVariadicHelper(const std::shared_ptr<T<Ts...>>& ctx,
                           THead&& head, TTail&&... tail) {
  using ValueType = typename std::decay<THead>::type::value_type;
  std::forward<THead>(head).setCallback_([ctx](Try<ValueType>&& t) {
    ctx->template setPartialResult<
        ValueType,
        sizeof...(Ts) - sizeof...(TTail)-1>(t);
  });
  // template tail-recursion
  collectVariadicHelper(ctx, std::forward<TTail>(tail)...);
}

} // namespace detail
} // namespace futures
} // namespace folly
