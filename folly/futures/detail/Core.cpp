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

#include <folly/futures/detail/Core.h>

#include <new>

#include <folly/lang/Assume.h>

namespace folly {
namespace futures {
namespace detail {

void UniqueDeleter::operator()(DeferredExecutor* ptr) {
  if (ptr) {
    ptr->release();
  }
}

KeepAliveOrDeferred::KeepAliveOrDeferred() noexcept : state_(State::Deferred) {
  ::new (&deferred_) DW{};
}

KeepAliveOrDeferred::KeepAliveOrDeferred(KA ka) noexcept
    : state_(State::KeepAlive) {
  ::new (&keepAlive_) KA{std::move(ka)};
}

KeepAliveOrDeferred::KeepAliveOrDeferred(DW deferred) noexcept
    : state_(State::Deferred) {
  ::new (&deferred_) DW{std::move(deferred)};
}

KeepAliveOrDeferred::KeepAliveOrDeferred(KeepAliveOrDeferred&& other) noexcept
    : state_(other.state_) {
  switch (state_) {
    case State::Deferred:
      ::new (&deferred_) DW{std::move(other.deferred_)};
      break;
    case State::KeepAlive:
      ::new (&keepAlive_) KA{std::move(other.keepAlive_)};
      break;
  }
}

KeepAliveOrDeferred::~KeepAliveOrDeferred() {
  switch (state_) {
    case State::Deferred:
      deferred_.~DW();
      break;
    case State::KeepAlive:
      keepAlive_.~KA();
      break;
  }
}

KeepAliveOrDeferred& KeepAliveOrDeferred::operator=(
    KeepAliveOrDeferred&& other) noexcept {
  // This is safe to do because KeepAliveOrDeferred is nothrow
  // move-constructible.
  this->~KeepAliveOrDeferred();
  ::new (this) KeepAliveOrDeferred{std::move(other)};
  return *this;
}

DeferredExecutor* KeepAliveOrDeferred::getDeferredExecutor() const noexcept {
  switch (state_) {
    case State::Deferred:
      return deferred_.get();
    case State::KeepAlive:
      return nullptr;
  }
  assume_unreachable();
}

Executor* KeepAliveOrDeferred::getKeepAliveExecutor() const noexcept {
  switch (state_) {
    case State::Deferred:
      return nullptr;
    case State::KeepAlive:
      return keepAlive_.get();
  }
  assume_unreachable();
}

KeepAliveOrDeferred::KA KeepAliveOrDeferred::stealKeepAlive() && noexcept {
  switch (state_) {
    case State::Deferred:
      return KA{};
    case State::KeepAlive:
      return std::move(keepAlive_);
  }
  assume_unreachable();
}

KeepAliveOrDeferred::DW KeepAliveOrDeferred::stealDeferred() && noexcept {
  switch (state_) {
    case State::Deferred:
      return std::move(deferred_);
    case State::KeepAlive:
      return DW{};
  }
  assume_unreachable();
}

KeepAliveOrDeferred KeepAliveOrDeferred::copy() const {
  switch (state_) {
    case State::Deferred:
      if (auto def = getDeferredExecutor()) {
        return KeepAliveOrDeferred{def->copy()};
      } else {
        return KeepAliveOrDeferred{};
      }
    case State::KeepAlive:
      return KeepAliveOrDeferred{keepAlive_};
  }
  assume_unreachable();
}

/* explicit */ KeepAliveOrDeferred::operator bool() const noexcept {
  return getDeferredExecutor() || getKeepAliveExecutor();
}

void DeferredExecutor::addFrom(
    Executor::KeepAlive<>&& completingKA,
    Executor::KeepAlive<>::KeepAliveFunc func) {
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::DETACHED) {
    return;
  }

  // If we are completing on the current executor, call inline, otherwise
  // add
  auto addWithInline =
      [&](Executor::KeepAlive<>::KeepAliveFunc&& addFunc) mutable {
        if (completingKA.get() == executor_.get()) {
          addFunc(std::move(completingKA));
        } else {
          executor_.copy().add(std::move(addFunc));
        }
      };

  if (state == State::HAS_EXECUTOR) {
    addWithInline(std::move(func));
    return;
  }
  DCHECK(state == State::EMPTY);
  func_ = std::move(func);
  if (folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::HAS_FUNCTION,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }
  DCHECK(state == State::DETACHED || state == State::HAS_EXECUTOR);
  if (state == State::DETACHED) {
    std::exchange(func_, nullptr);
    return;
  }
  addWithInline(std::exchange(func_, nullptr));
}

Executor* DeferredExecutor::getExecutor() const {
  assert(executor_.get());
  return executor_.get();
}

void DeferredExecutor::setExecutor(folly::Executor::KeepAlive<> executor) {
  if (nestedExecutors_) {
    auto nestedExecutors = std::exchange(nestedExecutors_, nullptr);
    for (auto& nestedExecutor : *nestedExecutors) {
      assert(nestedExecutor.get());
      nestedExecutor.get()->setExecutor(executor.copy());
    }
  }
  executor_ = std::move(executor);
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::EMPTY &&
      folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::HAS_EXECUTOR,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }

  DCHECK(state == State::HAS_FUNCTION);
  state_.store(State::HAS_EXECUTOR, std::memory_order_release);
  executor_.copy().add(std::exchange(func_, nullptr));
}

void DeferredExecutor::setNestedExecutors(
    std::vector<DeferredWrapper> executors) {
  DCHECK(!nestedExecutors_);
  nestedExecutors_ =
      std::make_unique<std::vector<DeferredWrapper>>(std::move(executors));
}

void DeferredExecutor::detach() {
  if (nestedExecutors_) {
    auto nestedExecutors = std::exchange(nestedExecutors_, nullptr);
    for (auto& nestedExecutor : *nestedExecutors) {
      assert(nestedExecutor.get());
      nestedExecutor.get()->detach();
    }
  }
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::EMPTY &&
      folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::DETACHED,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }

  DCHECK(state == State::HAS_FUNCTION);
  state_.store(State::DETACHED, std::memory_order_release);
  std::exchange(func_, nullptr);
}

DeferredWrapper DeferredExecutor::copy() {
  acquire();
  return DeferredWrapper(this);
}

/* static */ DeferredWrapper DeferredExecutor::create() {
  return DeferredWrapper(new DeferredExecutor{});
}

DeferredExecutor::DeferredExecutor() {}

void DeferredExecutor::acquire() {
  auto keepAliveCount = keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
  DCHECK_GT(keepAliveCount, 0);
}

void DeferredExecutor::release() {
  auto keepAliveCount = keepAliveCount_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK_GT(keepAliveCount, 0);
  if (keepAliveCount == 1) {
    delete this;
  }
}

InterruptHandler::~InterruptHandler() = default;

void InterruptHandler::acquire() {
  auto refCount = refCount_.fetch_add(1, std::memory_order_relaxed);
  DCHECK_GT(refCount, 0);
}

void InterruptHandler::release() {
  auto refCount = refCount_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK_GT(refCount, 0);
  if (refCount == 1) {
    delete this;
  }
}

bool CoreBase::hasResult() const noexcept {
  constexpr auto allowed = State::OnlyResult | State::Done;
  auto core = this;
  auto state = core->state_.load(std::memory_order_acquire);
  while (state == State::Proxy) {
    core = core->proxy_;
    state = core->state_.load(std::memory_order_acquire);
  }
  return State() != (state & allowed);
}

Executor* CoreBase::getExecutor() const {
  if (!executor_.isKeepAlive()) {
    return nullptr;
  }
  return executor_.getKeepAliveExecutor();
}

DeferredExecutor* CoreBase::getDeferredExecutor() const {
  if (!executor_.isDeferred()) {
    return {};
  }

  return executor_.getDeferredExecutor();
}

DeferredWrapper CoreBase::stealDeferredExecutor() {
  if (executor_.isKeepAlive()) {
    return {};
  }

  return std::move(executor_).stealDeferred();
}

void CoreBase::raise(exception_wrapper e) {
  std::lock_guard<SpinLock> lock(interruptLock_);
  if (!interrupt_ && !hasResult()) {
    interrupt_ = std::make_unique<exception_wrapper>(std::move(e));
    auto interruptHandler = interruptHandler_.load(std::memory_order_relaxed);
    if (interruptHandler) {
      interruptHandler->handle(*interrupt_);
    }
  }
}

void CoreBase::initCopyInterruptHandlerFrom(const CoreBase& other) {
  auto interruptHandler =
      other.interruptHandler_.load(std::memory_order_acquire);
  if (interruptHandler != nullptr) {
    interruptHandler->acquire();
  }
  auto oldInterruptHandler =
      interruptHandler_.exchange(interruptHandler, std::memory_order_release);
  DCHECK(oldInterruptHandler == nullptr);
}

class CoreBase::CoreAndCallbackReference {
 public:
  explicit CoreAndCallbackReference(CoreBase* core) noexcept : core_(core) {}

  ~CoreAndCallbackReference() noexcept { detach(); }

  CoreAndCallbackReference(CoreAndCallbackReference const& o) = delete;
  CoreAndCallbackReference& operator=(CoreAndCallbackReference const& o) =
      delete;
  CoreAndCallbackReference& operator=(CoreAndCallbackReference&&) = delete;

  CoreAndCallbackReference(CoreAndCallbackReference&& o) noexcept
      : core_(std::exchange(o.core_, nullptr)) {}

  CoreBase* getCore() const noexcept { return core_; }

 private:
  void detach() noexcept {
    if (core_) {
      core_->derefCallback();
      core_->detachOne();
    }
  }

  CoreBase* core_{nullptr};
};

CoreBase::CoreBase(State state, unsigned char attached)
    : state_(state), attached_(attached) {}

CoreBase::~CoreBase() {
  auto interruptHandler = interruptHandler_.load(std::memory_order_relaxed);
  if (interruptHandler != nullptr) {
    interruptHandler->release();
  }
}

void CoreBase::setCallback_(
    Callback&& callback,
    std::shared_ptr<folly::RequestContext>&& context,
    futures::detail::InlineContinuation allowInline) {
  DCHECK(!hasCallback());

  ::new (&callback_) Callback(std::move(callback));
  ::new (&context_) Context(std::move(context));

  auto state = state_.load(std::memory_order_acquire);
  State nextState = allowInline == futures::detail::InlineContinuation::permit
      ? State::OnlyCallbackAllowInline
      : State::OnlyCallback;

  if (state == State::Start) {
    if (folly::atomic_compare_exchange_strong_explicit(
            &state_,
            &state,
            nextState,
            std::memory_order_release,
            std::memory_order_acquire)) {
      return;
    }
    assume(state == State::OnlyResult || state == State::Proxy);
  }

  if (state == State::OnlyResult) {
    state_.store(State::Done, std::memory_order_relaxed);
    doCallback(Executor::KeepAlive<>{}, state);
    return;
  }

  if (state == State::Proxy) {
    return proxyCallback(state);
  }

  terminate_with<std::logic_error>("setCallback unexpected state");
}

void CoreBase::setResult_(Executor::KeepAlive<>&& completingKA) {
  DCHECK(!hasResult());

  auto state = state_.load(std::memory_order_acquire);
  switch (state) {
    case State::Start:
      if (folly::atomic_compare_exchange_strong_explicit(
              &state_,
              &state,
              State::OnlyResult,
              std::memory_order_release,
              std::memory_order_acquire)) {
        return;
      }
      assume(
          state == State::OnlyCallback ||
          state == State::OnlyCallbackAllowInline);
      FOLLY_FALLTHROUGH;

    case State::OnlyCallback:
    case State::OnlyCallbackAllowInline:
      state_.store(State::Done, std::memory_order_relaxed);
      doCallback(std::move(completingKA), state);
      return;
    case State::OnlyResult:
    case State::Proxy:
    case State::Done:
    case State::Empty:
    default:
      terminate_with<std::logic_error>("setResult unexpected state");
  }
}

void CoreBase::setProxy_(CoreBase* proxy) {
  DCHECK(!hasResult());

  proxy_ = proxy;

  auto state = state_.load(std::memory_order_acquire);
  switch (state) {
    case State::Start:
      if (folly::atomic_compare_exchange_strong_explicit(
              &state_,
              &state,
              State::Proxy,
              std::memory_order_release,
              std::memory_order_acquire)) {
        break;
      }
      assume(
          state == State::OnlyCallback ||
          state == State::OnlyCallbackAllowInline);
      FOLLY_FALLTHROUGH;

    case State::OnlyCallback:
    case State::OnlyCallbackAllowInline:
      proxyCallback(state);
      break;
    case State::OnlyResult:
    case State::Proxy:
    case State::Done:
    case State::Empty:
    default:
      terminate_with<std::logic_error>("setCallback unexpected state");
  }

  detachOne();
}

// May be called at most once.
void CoreBase::doCallback(
    Executor::KeepAlive<>&& completingKA, State priorState) {
  DCHECK(state_ == State::Done);

  auto executor = std::exchange(executor_, KeepAliveOrDeferred{});

  // Customise inline behaviour
  // If addCompletingKA is non-null, then we are allowing inline execution
  auto doAdd = [](Executor::KeepAlive<>&& addCompletingKA,
                  KeepAliveOrDeferred&& currentExecutor,
                  auto&& keepAliveFunc) mutable {
    if (auto deferredExecutorPtr = currentExecutor.getDeferredExecutor()) {
      deferredExecutorPtr->addFrom(
          std::move(addCompletingKA), std::move(keepAliveFunc));
    } else {
      // If executors match call inline
      auto currentKeepAlive = std::move(currentExecutor).stealKeepAlive();
      if (addCompletingKA.get() == currentKeepAlive.get()) {
        keepAliveFunc(std::move(currentKeepAlive));
      } else {
        std::move(currentKeepAlive).add(std::move(keepAliveFunc));
      }
    }
  };

  if (executor) {
    // If we are not allowing inline, clear the completing KA to disallow
    if (!(priorState == State::OnlyCallbackAllowInline)) {
      completingKA = Executor::KeepAlive<>{};
    }
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
    attached_.fetch_add(2, std::memory_order_relaxed);
    callbackReferences_.fetch_add(2, std::memory_order_relaxed);
    CoreAndCallbackReference guard_local_scope(this);
    CoreAndCallbackReference guard_lambda(this);
    try {
      doAdd(
          std::move(completingKA),
          std::move(executor),
          [core_ref =
               std::move(guard_lambda)](Executor::KeepAlive<>&& ka) mutable {
            auto cr = std::move(core_ref);
            CoreBase* const core = cr.getCore();
            RequestContextScopeGuard rctx(std::move(core->context_));
            core->callback_(*core, std::move(ka), nullptr);
          });
    } catch (const std::exception& e) {
      ew = exception_wrapper(std::current_exception(), e);
    } catch (...) {
      ew = exception_wrapper(std::current_exception());
    }
    if (ew) {
      RequestContextScopeGuard rctx(std::move(context_));
      callback_(*this, Executor::KeepAlive<>{}, &ew);
    }
  } else {
    attached_.fetch_add(1, std::memory_order_relaxed);
    SCOPE_EXIT {
      context_.~Context();
      callback_.~Callback();
      detachOne();
    };
    RequestContextScopeGuard rctx(std::move(context_));
    callback_(*this, std::move(completingKA), nullptr);
  }
}

void CoreBase::proxyCallback(State priorState) {
  // If the state of the core being proxied had a callback that allows inline
  // execution, maintain this information in the proxy
  futures::detail::InlineContinuation allowInline =
      (priorState == State::OnlyCallbackAllowInline
           ? futures::detail::InlineContinuation::permit
           : futures::detail::InlineContinuation::forbid);
  state_.store(State::Empty, std::memory_order_relaxed);
  proxy_->setExecutor(std::move(executor_));
  proxy_->setCallback_(std::move(callback_), std::move(context_), allowInline);
  proxy_->detachFuture();
  context_.~Context();
  callback_.~Callback();
}

void CoreBase::detachOne() noexcept {
  auto a = attached_.fetch_sub(1, std::memory_order_acq_rel);
  assert(a >= 1);
  if (a == 1) {
    delete this;
  }
}

void CoreBase::derefCallback() noexcept {
  auto c = callbackReferences_.fetch_sub(1, std::memory_order_acq_rel);
  assert(c >= 1);
  if (c == 1) {
    context_.~Context();
    callback_.~Callback();
  }
}

#if FOLLY_USE_EXTERN_FUTURE_UNIT
template class Core<folly::Unit>;
#endif

} // namespace detail
} // namespace futures
} // namespace folly
