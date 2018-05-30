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
#include <folly/futures/detail/FSM.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Exception.h>
#include <folly/synchronization/MicroSpinLock.h>

#include <folly/io/async/Request.h>

namespace folly {
namespace futures {
namespace detail {

/*
        OnlyCallback
       /            \
  Start              Done
       \            /
         OnlyResult

This state machine is fairly self-explanatory. The most important bit is
that the callback is only executed just after the transition from Only* to Done.
*/
enum class State : uint8_t {
  Start,
  OnlyResult,
  OnlyCallback,
  Done,
};

/// SpinLock is and must stay a 1-byte object because of how Core is laid out.
struct SpinLock : private MicroSpinLock {
  SpinLock() : MicroSpinLock{0} {}

  using MicroSpinLock::lock;
  using MicroSpinLock::unlock;
};
static_assert(sizeof(SpinLock) == 1, "missized");

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
  static Core* make() {
    return new Core();
  }

  static Core* make(Try<T>&& t) {
    return new Core(std::move(t));
  }

  template <typename... Args>
  static Core<T>* make(in_place_t, Args&&... args) {
    return new Core<T>(in_place, std::forward<Args>(args)...);
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
    return getTryImpl(*this);
  }
  Try<T> const& getTry() const {
    return getTryImpl(*this);
  }

  /// Call only from Future thread.
  template <typename F>
  void setCallback(F&& func) {
    auto setCallback_ = [&]{
      context_ = RequestContext::saveContext();
      callback_ = std::forward<F>(func);
    };

    fsm_.transition([&](State state) {
      switch (state) {
        case State::Start:
          return fsm_.tryUpdateState(state, State::OnlyCallback, setCallback_);

        case State::OnlyResult:
          return fsm_.tryUpdateState(
              state, State::Done, setCallback_, [&] { doCallback(); });

        case State::OnlyCallback:
        case State::Done:
          throw_exception<std::logic_error>("setCallback called twice");
      }
      assume_unreachable();
    });
  }

  /// Call only from Promise thread
  void setResult(Try<T>&& t) {
    auto setResult_ = [&]{ result_ = std::move(t); };
    fsm_.transition([&](State state) {
      switch (state) {
        case State::Start:
          return fsm_.tryUpdateState(state, State::OnlyResult, setResult_);

        case State::OnlyCallback:
          return fsm_.tryUpdateState(
              state, State::Done, setResult_, [&] { doCallback(); });

        case State::OnlyResult:
        case State::Done:
          throw_exception<std::logic_error>("setResult called twice");
      }
      assume_unreachable();
    });
  }

  /// Called by a destructing Future (in the Future thread, by definition)
  void detachFuture() noexcept {
    detachOne();
  }

  /// Called by a destructing Promise (in the Promise thread, by definition)
  void detachPromise() noexcept {
    DCHECK(result_);
    detachOne();
  }

  /// Call only from Future thread, either before attaching a callback or after
  /// the callback has already been invoked, but not concurrently with anything
  /// which might trigger invocation of the callback
  void setExecutor(
      Executor::KeepAlive<> x,
      int8_t priority = Executor::MID_PRI) {
    DCHECK(fsm_.getState() != State::OnlyCallback);
    executor_ = std::move(x);
    priority_ = priority;
  }

  void setExecutor(Executor* x, int8_t priority = Executor::MID_PRI) {
    setExecutor(getKeepAliveToken(x), priority);
  }

  Executor* getExecutor() const {
    return executor_.get();
  }

  /// Call only from Future thread
  void raise(exception_wrapper e) {
    std::lock_guard<SpinLock> lock(interruptLock_);
    if (!interrupt_ && !hasResult()) {
      interrupt_ = std::make_unique<exception_wrapper>(std::move(e));
      if (interruptHandler_) {
        interruptHandler_(*interrupt_);
      }
    }
  }

  std::function<void(exception_wrapper const&)> getInterruptHandler() {
    if (!interruptHandlerSet_.load(std::memory_order_acquire)) {
      return nullptr;
    }
    std::lock_guard<SpinLock> lock(interruptLock_);
    return interruptHandler_;
  }

  /// Call only from Promise thread
  template <typename F>
  void setInterruptHandler(F&& fn) {
    std::lock_guard<SpinLock> lock(interruptLock_);
    if (!hasResult()) {
      if (interrupt_) {
        fn(as_const(*interrupt_));
      } else {
        setInterruptHandlerNoLock(std::forward<F>(fn));
      }
    }
  }

  void setInterruptHandlerNoLock(
      std::function<void(exception_wrapper const&)> fn) {
    interruptHandlerSet_.store(true, std::memory_order_relaxed);
    interruptHandler_ = std::move(fn);
  }

 private:
  Core() : result_(), fsm_(State::Start), attached_(2) {}

  explicit Core(Try<T>&& t)
      : result_(std::move(t)), fsm_(State::OnlyResult), attached_(1) {}

  template <typename... Args>
  explicit Core(in_place_t, Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args&&...>::value)
      : result_(in_place, in_place, std::forward<Args>(args)...),
        fsm_(State::OnlyResult),
        attached_(1) {}

  ~Core() {
    DCHECK(attached_ == 0);
    DCHECK(result_);
  }

  // Helper class that stores a pointer to the `Core` object and calls
  // `derefCallback` and `detachOne` in the destructor.
  class CoreAndCallbackReference {
   public:
    explicit CoreAndCallbackReference(Core* core) noexcept : core_(core) {}

    ~CoreAndCallbackReference() noexcept {
      detach();
    }

    CoreAndCallbackReference(CoreAndCallbackReference const& o) = delete;
    CoreAndCallbackReference& operator=(CoreAndCallbackReference const& o) =
        delete;
    CoreAndCallbackReference& operator=(CoreAndCallbackReference&&) = delete;

    CoreAndCallbackReference(CoreAndCallbackReference&& o) noexcept
        : core_(exchange(o.core_, nullptr)) {}

    Core* getCore() const noexcept {
      return core_;
    }

   private:
    void detach() noexcept {
      if (core_) {
        core_->derefCallback();
        core_->detachOne();
      }
    }

    Core* core_{nullptr};
  };

  void doCallback() {
    DCHECK(fsm_.getState() == State::Done);
    auto x = exchange(executor_, Executor::KeepAlive<>());
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
        auto xPtr = x.get();
        if (LIKELY(x->getNumPriorities() == 1)) {
          xPtr->add([core_ref = std::move(guard_lambda),
                     keepAlive = std::move(x)]() mutable {
            auto cr = std::move(core_ref);
            Core* const core = cr.getCore();
            RequestContextScopeGuard rctx(core->context_);
            core->callback_(std::move(*core->result_));
          });
        } else {
          xPtr->addWithPriority(
              [core_ref = std::move(guard_lambda),
               keepAlive = std::move(x)]() mutable {
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

  void detachOne() noexcept {
    auto a = attached_--;
    assert(a >= 1);
    if (a == 1) {
      delete this;
    }
  }

  void derefCallback() noexcept {
    if (--callbackReferences_ == 0) {
      callback_ = {};
    }
  }

  template <typename Self>
  static decltype(auto) getTryImpl(Self& self) {
    assume(self.result_.has_value());
    return self.result_.value();
  }

  folly::Function<void(Try<T>&&)> callback_;
  // place result_ next to increase the likelihood that the value will be
  // contained entirely in one cache line
  folly::Optional<Try<T>> result_;
  FSM<State, SpinLock> fsm_;
  std::atomic<unsigned char> attached_;
  std::atomic<unsigned char> callbackReferences_{0};
  std::atomic<bool> interruptHandlerSet_ {false};
  SpinLock interruptLock_;
  int8_t priority_ {-1};
  Executor::KeepAlive<> executor_;
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
