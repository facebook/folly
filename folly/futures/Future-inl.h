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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>

#include <folly/Optional.h>
#include <folly/executors/InlineExecutor.h>
#include <folly/futures/Timekeeper.h>
#include <folly/futures/detail/Core.h>
#include <folly/synchronization/Baton.h>

#ifndef FOLLY_FUTURE_USING_FIBER
#if FOLLY_MOBILE || defined(__APPLE__)
#define FOLLY_FUTURE_USING_FIBER 0
#else
#define FOLLY_FUTURE_USING_FIBER 1
#include <folly/fibers/Baton.h>
#endif
#endif

namespace folly {

class Timekeeper;

namespace futures {
namespace detail {
#if FOLLY_FUTURE_USING_FIBER
typedef folly::fibers::Baton FutureBatonType;
#else
typedef folly::Baton<> FutureBatonType;
#endif
} // namespace detail
} // namespace futures

namespace detail {
std::shared_ptr<Timekeeper> getTimekeeperSingleton();
} // namespace detail

namespace futures {
namespace detail {
//  Guarantees that the stored functor is destructed before the stored promise
//  may be fulfilled. Assumes the stored functor to be noexcept-destructible.
template <typename T, typename F>
class CoreCallbackState {
 public:
  template <typename FF>
  CoreCallbackState(Promise<T>&& promise, FF&& func) noexcept(
      noexcept(F(std::declval<FF>())))
      : func_(std::forward<FF>(func)), promise_(std::move(promise)) {
    assert(before_barrier());
  }

  CoreCallbackState(CoreCallbackState&& that) noexcept(
      noexcept(F(std::declval<F>()))) {
    if (that.before_barrier()) {
      new (&func_) F(std::move(that.func_));
      promise_ = that.stealPromise();
    }
  }

  CoreCallbackState& operator=(CoreCallbackState&&) = delete;

  ~CoreCallbackState() {
    if (before_barrier()) {
      stealPromise();
    }
  }

  template <typename... Args>
  auto invoke(Args&&... args) noexcept(
      noexcept(std::declval<F&&>()(std::declval<Args&&>()...))) {
    assert(before_barrier());
    return std::move(func_)(std::forward<Args>(args)...);
  }

  template <typename... Args>
  auto tryInvoke(Args&&... args) noexcept {
    return makeTryWith([&] { return invoke(std::forward<Args>(args)...); });
  }

  void setTry(Try<T>&& t) {
    stealPromise().setTry(std::move(t));
  }

  void setException(exception_wrapper&& ew) {
    stealPromise().setException(std::move(ew));
  }

  Promise<T> stealPromise() noexcept {
    assert(before_barrier());
    func_.~F();
    return std::move(promise_);
  }

 private:
  bool before_barrier() const noexcept {
    return !promise_.isFulfilled();
  }

  union {
    F func_;
  };
  Promise<T> promise_{Promise<T>::makeEmpty()};
};

template <typename T, typename F>
inline auto makeCoreCallbackState(Promise<T>&& p, F&& f) noexcept(
    noexcept(CoreCallbackState<T, _t<std::decay<F>>>(
        std::declval<Promise<T>&&>(),
        std::declval<F&&>()))) {
  return CoreCallbackState<T, _t<std::decay<F>>>(
      std::move(p), std::forward<F>(f));
}

template <class T>
FutureBase<T>::FutureBase(SemiFuture<T>&& other) noexcept : core_(other.core_) {
  other.core_ = nullptr;
}

template <class T>
FutureBase<T>::FutureBase(Future<T>&& other) noexcept : core_(other.core_) {
  other.core_ = nullptr;
}

template <class T>
template <class T2, typename>
FutureBase<T>::FutureBase(T2&& val)
    : core_(new futures::detail::Core<T>(Try<T>(std::forward<T2>(val)))) {}

template <class T>
template <typename T2>
FutureBase<T>::FutureBase(
    typename std::enable_if<std::is_same<Unit, T2>::value>::type*)
    : core_(new futures::detail::Core<T>(Try<T>(T()))) {}

template <class T>
template <
    class... Args,
    typename std::enable_if<std::is_constructible<T, Args&&...>::value, int>::
        type>
FutureBase<T>::FutureBase(in_place_t, Args&&... args)
    : core_(
          new futures::detail::Core<T>(in_place, std::forward<Args>(args)...)) {
}

template <class T>
template <class FutureType>
void FutureBase<T>::assign(FutureType& other) noexcept {
  std::swap(core_, other.core_);
}

template <class T>
FutureBase<T>::~FutureBase() {
  detach();
}

template <class T>
T& FutureBase<T>::value() & {
  return result().value();
}

template <class T>
T const& FutureBase<T>::value() const& {
  return result().value();
}

template <class T>
T&& FutureBase<T>::value() && {
  return std::move(result().value());
}

template <class T>
T const&& FutureBase<T>::value() const&& {
  return std::move(result().value());
}

template <class T>
Try<T>& FutureBase<T>::result() & {
  throwIfInvalid();

  return core_->getTry();
}

template <class T>
Try<T> const& FutureBase<T>::result() const& {
  throwIfInvalid();

  return core_->getTry();
}

template <class T>
Try<T>&& FutureBase<T>::result() && {
  throwIfInvalid();

  return std::move(core_->getTry());
}

template <class T>
Try<T> const&& FutureBase<T>::result() const&& {
  throwIfInvalid();

  return std::move(core_->getTry());
}

template <class T>
bool FutureBase<T>::isReady() const {
  throwIfInvalid();
  return core_->ready();
}

template <class T>
bool FutureBase<T>::hasValue() {
  return core_->getTry().hasValue();
}

template <class T>
bool FutureBase<T>::hasException() {
  return core_->getTry().hasException();
}

template <class T>
void FutureBase<T>::detach() {
  if (core_) {
    core_->detachFuture();
    core_ = nullptr;
  }
}

template <class T>
void FutureBase<T>::throwIfInvalid() const {
  if (!core_) {
    throwNoState();
  }
}

template <class T>
Optional<Try<T>> FutureBase<T>::poll() {
  Optional<Try<T>> o;
  if (core_->ready()) {
    o = std::move(core_->getTry());
  }
  return o;
}

template <class T>
void FutureBase<T>::raise(exception_wrapper exception) {
  core_->raise(std::move(exception));
}

template <class T>
template <class F>
void FutureBase<T>::setCallback_(F&& func) {
  throwIfInvalid();
  core_->setCallback(std::forward<F>(func));
}

template <class T>
FutureBase<T>::FutureBase(futures::detail::EmptyConstruct) noexcept
    : core_(nullptr) {}

// then

// Variant: returns a value
// e.g. f.then([](Try<T>&& t){ return t.value(); });
template <class T>
template <typename F, typename R, bool isTry, typename... Args>
typename std::enable_if<!R::ReturnsFuture::value, typename R::Return>::type
FutureBase<T>::thenImplementation(
    F&& func,
    futures::detail::argResult<isTry, F, Args...>) {
  static_assert(sizeof...(Args) <= 1, "Then must take zero/one argument");
  typedef typename R::ReturnsFuture::Inner B;

  this->throwIfInvalid();

  Promise<B> p;
  p.core_->setInterruptHandlerNoLock(this->core_->getInterruptHandler());

  // grab the Future now before we lose our handle on the Promise
  auto sf = p.getSemiFuture();
  sf.core_->setExecutor(this->getExecutor());
  auto f = Future<B>(sf.core_);
  sf.core_ = nullptr;

  /* This is a bit tricky.

     We can't just close over *this in case this Future gets moved. So we
     make a new dummy Future. We could figure out something more
     sophisticated that avoids making a new Future object when it can, as an
     optimization. But this is correct.

     core_ can't be moved, it is explicitly disallowed (as is copying). But
     if there's ever a reason to allow it, this is one place that makes that
     assumption and would need to be fixed. We use a standard shared pointer
     for core_ (by copying it in), which means in essence obj holds a shared
     pointer to itself.  But this shouldn't leak because Promise will not
     outlive the continuation, because Promise will setException() with a
     broken Promise if it is destructed before completed. We could use a
     weak pointer but it would have to be converted to a shared pointer when
     func is executed (because the Future returned by func may possibly
     persist beyond the callback, if it gets moved), and so it is an
     optimization to just make it shared from the get-go.

     Two subtle but important points about this design. futures::detail::Core
     has no back pointers to Future or Promise, so if Future or Promise get
     moved (and they will be moved in performant code) we don't have to do
     anything fancy. And because we store the continuation in the
     futures::detail::Core, not in the Future, we can execute the continuation
     even after the Future has gone out of scope. This is an intentional design
     decision. It is likely we will want to be able to cancel a continuation
     in some circumstances, but I think it should be explicit not implicit
     in the destruction of the Future used to create it.
     */
  this->setCallback_(
      [state = futures::detail::makeCoreCallbackState(
           std::move(p), std::forward<F>(func))](Try<T>&& t) mutable {

        if (!isTry && t.hasException()) {
          state.setException(std::move(t.exception()));
        } else {
          state.setTry(makeTryWith(
              [&] { return state.invoke(t.template get<isTry, Args>()...); }));
        }
      });
  return f;
}

// Pass through a simple future as it needs no deferral adaptation
template <class T>
Future<T> chainExecutor(Executor*, Future<T>&& f) {
  return std::move(f);
}

// Correctly chain a SemiFuture for deferral
template <class T>
Future<T> chainExecutor(Executor* e, SemiFuture<T>&& f) {
  if (!e) {
    e = &folly::InlineExecutor::instance();
  }
  return std::move(f).via(e);
}

// Variant: returns a Future
// e.g. f.then([](T&& t){ return makeFuture<T>(t); });
template <class T>
template <typename F, typename R, bool isTry, typename... Args>
typename std::enable_if<R::ReturnsFuture::value, typename R::Return>::type
FutureBase<T>::thenImplementation(
    F&& func,
    futures::detail::argResult<isTry, F, Args...>) {
  static_assert(sizeof...(Args) <= 1, "Then must take zero/one argument");
  typedef typename R::ReturnsFuture::Inner B;
  this->throwIfInvalid();

  Promise<B> p;
  p.core_->setInterruptHandlerNoLock(this->core_->getInterruptHandler());

  // grab the Future now before we lose our handle on the Promise
  auto sf = p.getSemiFuture();
  auto* e = this->getExecutor();
  sf.core_->setExecutor(e);
  auto f = Future<B>(sf.core_);
  sf.core_ = nullptr;

  this->setCallback_([state = futures::detail::makeCoreCallbackState(
                          std::move(p), std::forward<F>(func))](
                         Try<T>&& t) mutable {
    if (!isTry && t.hasException()) {
      state.setException(std::move(t.exception()));
    } else {
      // Ensure that if function returned a SemiFuture we correctly chain
      // potential deferral.
      auto tf2 = state.tryInvoke(t.template get<isTry, Args>()...);
      if (tf2.hasException()) {
        state.setException(std::move(tf2.exception()));
      } else {
        auto statePromise = state.stealPromise();
        auto tf3 =
            chainExecutor(statePromise.core_->getExecutor(), *std::move(tf2));
        tf3.setCallback_([p2 = std::move(statePromise)](Try<B>&& b) mutable {
          p2.setTry(std::move(b));
        });
      }
    }
  });

  return f;
}

/**
 * Defer work until executor is actively boosted.
 *
 * NOTE: that this executor is a private implementation detail belonging to the
 * Folly Futures library and not intended to be used elsewhere. It is designed
 * specifically for the use case of deferring work on a SemiFuture. It is NOT
 * thread safe. Please do not use for any other purpose without great care.
 */
class DeferredExecutor final : public Executor {
 public:
  void add(Func func) override {
    auto state = state_.load(std::memory_order_acquire);
    if (state == State::HAS_FUNCTION) {
      // This means we are inside runAndDestroy, just run the function inline
      func();
      return;
    }

    func_ = std::move(func);
    std::shared_ptr<FutureBatonType> baton;
    do {
      if (state == State::HAS_EXECUTOR) {
        state_.store(State::HAS_FUNCTION, std::memory_order_release);
        executor_->add([this] { this->runAndDestroy(); });
        return;
      }
      if (state == State::DETACHED) {
        // Function destructor may trigger more functions to be added to the
        // Executor. They should be run inline.
        state = State::HAS_FUNCTION;
        func_ = nullptr;
        delete this;
        return;
      }
      if (state == State::HAS_BATON) {
        baton = baton_.copy();
      }
      assert(state == State::EMPTY || state == State::HAS_BATON);
    } while (!state_.compare_exchange_weak(
        state,
        State::HAS_FUNCTION,
        std::memory_order_release,
        std::memory_order_acquire));

    // After compare_exchange_weak is complete, we can no longer use this
    // object since it may be destroyed from another thread.
    if (baton) {
      baton->post();
    }
  }

  void setExecutor(folly::Executor* executor) {
    executor_ = executor;
    auto state = state_.load(std::memory_order_acquire);
    do {
      if (state == State::HAS_FUNCTION) {
        executor_->add([this] { this->runAndDestroy(); });
        return;
      }
      assert(state == State::EMPTY);
    } while (!state_.compare_exchange_weak(
        state,
        State::HAS_EXECUTOR,
        std::memory_order_release,
        std::memory_order_acquire));
  }

  void runAndDestroy() {
    assert(state_.load(std::memory_order_relaxed) == State::HAS_FUNCTION);
    func_();
    delete this;
  }

  void detach() {
    auto state = state_.load(std::memory_order_acquire);
    do {
      if (state == State::HAS_FUNCTION) {
        // Function destructor may trigger more functions to be added to the
        // Executor. They should be run inline.
        func_ = nullptr;
        delete this;
        return;
      }

      assert(state == State::EMPTY);
    } while (!state_.compare_exchange_weak(
        state,
        State::DETACHED,
        std::memory_order_release,
        std::memory_order_acquire));
  }

  void wait() {
    auto state = state_.load(std::memory_order_acquire);
    auto baton = std::make_shared<FutureBatonType>();
    baton_ = baton;
    do {
      if (state == State::HAS_FUNCTION) {
        return;
      }
      assert(state == State::EMPTY);
    } while (!state_.compare_exchange_weak(
        state,
        State::HAS_BATON,
        std::memory_order_release,
        std::memory_order_acquire));

    baton->wait();

    assert(state_.load(std::memory_order_relaxed) == State::HAS_FUNCTION);
  }

  bool wait(Duration duration) {
    auto state = state_.load(std::memory_order_acquire);
    auto baton = std::make_shared<FutureBatonType>();
    baton_ = baton;
    do {
      if (state == State::HAS_FUNCTION) {
        return true;
      }
      assert(state == State::EMPTY);
    } while (!state_.compare_exchange_weak(
        state,
        State::HAS_BATON,
        std::memory_order_release,
        std::memory_order_acquire));

    if (baton->try_wait_for(duration)) {
      assert(state_.load(std::memory_order_relaxed) == State::HAS_FUNCTION);
      return true;
    }

    state = state_.load(std::memory_order_acquire);
    do {
      if (state == State::HAS_FUNCTION) {
        return true;
      }
      assert(state == State::HAS_BATON);
    } while (!state_.compare_exchange_weak(
        state,
        State::EMPTY,
        std::memory_order_release,
        std::memory_order_acquire));
    return false;
  }

 private:
  enum class State {
    EMPTY,
    HAS_FUNCTION,
    HAS_EXECUTOR,
    HAS_BATON,
    DETACHED,
  };
  std::atomic<State> state_{State::EMPTY};
  Func func_;
  Executor* executor_;
  folly::Synchronized<std::shared_ptr<FutureBatonType>> baton_;
};
} // namespace detail
} // namespace futures

template <class T>
SemiFuture<typename std::decay<T>::type> makeSemiFuture(T&& t) {
  return makeSemiFuture(Try<typename std::decay<T>::type>(std::forward<T>(t)));
}

// makeSemiFutureWith(SemiFuture<T>()) -> SemiFuture<T>
template <class F>
typename std::enable_if<
    isSemiFuture<typename std::result_of<F()>::type>::value,
    typename std::result_of<F()>::type>::type
makeSemiFutureWith(F&& func) {
  using InnerType =
      typename isSemiFuture<typename std::result_of<F()>::type>::Inner;
  try {
    return std::forward<F>(func)();
  } catch (std::exception& e) {
    return makeSemiFuture<InnerType>(
        exception_wrapper(std::current_exception(), e));
  } catch (...) {
    return makeSemiFuture<InnerType>(
        exception_wrapper(std::current_exception()));
  }
}

// makeSemiFutureWith(T()) -> SemiFuture<T>
// makeSemiFutureWith(void()) -> SemiFuture<Unit>
template <class F>
typename std::enable_if<
    !(isSemiFuture<typename std::result_of<F()>::type>::value),
    SemiFuture<Unit::LiftT<typename std::result_of<F()>::type>>>::type
makeSemiFutureWith(F&& func) {
  using LiftedResult = Unit::LiftT<typename std::result_of<F()>::type>;
  return makeSemiFuture<LiftedResult>(
      makeTryWith([&func]() mutable { return std::forward<F>(func)(); }));
}

template <class T>
SemiFuture<T> makeSemiFuture(std::exception_ptr const& e) {
  return makeSemiFuture(Try<T>(e));
}

template <class T>
SemiFuture<T> makeSemiFuture(exception_wrapper ew) {
  return makeSemiFuture(Try<T>(std::move(ew)));
}

template <class T, class E>
typename std::
    enable_if<std::is_base_of<std::exception, E>::value, SemiFuture<T>>::type
    makeSemiFuture(E const& e) {
  return makeSemiFuture(Try<T>(make_exception_wrapper<E>(e)));
}

template <class T>
SemiFuture<T> makeSemiFuture(Try<T>&& t) {
  return SemiFuture<T>(new futures::detail::Core<T>(std::move(t)));
}

// This must be defined after the constructors to avoid a bug in MSVC
// https://connect.microsoft.com/VisualStudio/feedback/details/3142777/out-of-line-constructor-definition-after-implicit-reference-causes-incorrect-c2244
inline SemiFuture<Unit> makeSemiFuture() {
  return makeSemiFuture(Unit{});
}

template <class T>
SemiFuture<T> SemiFuture<T>::makeEmpty() {
  return SemiFuture<T>(futures::detail::EmptyConstruct{});
}

template <class T>
typename SemiFuture<T>::DeferredExecutor* SemiFuture<T>::getDeferredExecutor()
    const {
  if (auto executor = this->core_->getExecutor()) {
    assert(dynamic_cast<DeferredExecutor*>(executor) != nullptr);
    return static_cast<DeferredExecutor*>(executor);
  }
  return nullptr;
}

template <class T>
void SemiFuture<T>::releaseDeferredExecutor(corePtr core) {
  if (!core) {
    return;
  }
  if (auto executor = core->getExecutor()) {
    assert(dynamic_cast<DeferredExecutor*>(executor) != nullptr);
    static_cast<DeferredExecutor*>(executor)->detach();
    core->setExecutor(nullptr);
  }
}

template <class T>
SemiFuture<T>::~SemiFuture() {
  releaseDeferredExecutor(this->core_);
}

template <class T>
SemiFuture<T>::SemiFuture(SemiFuture<T>&& other) noexcept
    : futures::detail::FutureBase<T>(std::move(other)) {}

template <class T>
SemiFuture<T>::SemiFuture(Future<T>&& other) noexcept
    : futures::detail::FutureBase<T>(std::move(other)) {
  // SemiFuture should not have an executor on construction
  if (this->core_) {
    this->setExecutor(nullptr);
  }
}

template <class T>
SemiFuture<T>& SemiFuture<T>::operator=(SemiFuture<T>&& other) noexcept {
  releaseDeferredExecutor(this->core_);
  this->assign(other);
  return *this;
}

template <class T>
SemiFuture<T>& SemiFuture<T>::operator=(Future<T>&& other) noexcept {
  releaseDeferredExecutor(this->core_);
  this->assign(other);
  // SemiFuture should not have an executor on construction
  if (this->core_) {
    this->setExecutor(nullptr);
  }
  return *this;
}

template <class T>
inline Future<T> SemiFuture<T>::via(Executor* executor, int8_t priority) && {
  throwIfInvalid();
  if (!executor) {
    throwNoExecutor();
  }

  if (auto deferredExecutor = getDeferredExecutor()) {
    deferredExecutor->setExecutor(executor);
  }

  auto newFuture = Future<T>(this->core_);
  this->core_ = nullptr;
  newFuture.setExecutor(executor, priority);

  return newFuture;
}

template <class T>
inline Future<T> SemiFuture<T>::toUnsafeFuture() && {
  return std::move(*this).via(&folly::InlineExecutor::instance());
}

template <class T>
template <typename F>
SemiFuture<
    typename futures::detail::deferCallableResult<T, F>::Return::value_type>
SemiFuture<T>::defer(F&& func) && {
  DeferredExecutor* deferredExecutor = getDeferredExecutor();
  if (!deferredExecutor) {
    deferredExecutor = new DeferredExecutor();
    this->setExecutor(deferredExecutor);
  }

  auto sf = Future<T>(this->core_).then(std::forward<F>(func)).semi();
  this->core_ = nullptr;
  // Carry deferred executor through chain as constructor from Future will
  // nullify it
  sf.setExecutor(deferredExecutor);
  return sf;
}

template <class T>
template <typename F>
SemiFuture<typename futures::detail::deferValueCallableResult<T, F>::Return::
               value_type>
SemiFuture<T>::deferValue(F&& func) && {
  return std::move(*this).defer(
      [f = std::forward<F>(func)](folly::Try<T>&& t) mutable {
        return f(t.template get<
                 false,
                 typename futures::detail::Extract<F>::FirstArg>());
      });
}

template <class T>
template <class F>
typename std::enable_if<
    !futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::callableWith<F, exception_wrapper&>::value &&
        !futures::detail::Extract<F>::ReturnsFuture::value,
    SemiFuture<T>>::type
SemiFuture<T>::deferError(F&& func) {
  using Exn =
      std::remove_reference_t<typename futures::detail::Extract<F>::FirstArg>;
  return std::move(*this).defer(
      [func = std::forward<F>(func)](Try<T>&& t) mutable {
        if (auto e = t.template tryGetExceptionObject<Exn>()) {
          return makeSemiFuture<T>(makeTryWith([&]() { return func(*e); }));
        } else {
          return makeSemiFuture<T>(std::move(t));
        }
      });
}

template <class T>
template <class F>
typename std::enable_if<
    !futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::callableWith<F, exception_wrapper&>::value &&
        futures::detail::Extract<F>::ReturnsFuture::value,
    SemiFuture<T>>::type
SemiFuture<T>::deferError(F&& func) {
  using Exn =
      std::remove_reference_t<typename futures::detail::Extract<F>::FirstArg>;
  return std::move(*this).defer(
      [func = std::forward<F>(func)](Try<T>&& t) mutable {
        if (auto e = t.template tryGetExceptionObject<Exn>()) {
          return func(*e);
        } else {
          return makeSemiFuture<T>(std::move(t));
        }
      });
}

template <class T>
template <class F>
typename std::enable_if<
    futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::Extract<F>::ReturnsFuture::value,
    SemiFuture<T>>::type
SemiFuture<T>::deferError(F&& func) {
  return std::move(*this).defer(
      [func = std::forward<F>(func)](Try<T> t) mutable {
        if (t.hasException()) {
          return makeSemiFuture<T>(func(std::move(t.exception())));
        } else {
          return makeSemiFuture<T>(std::move(t));
        }
      });
}

template <class T>
template <class F>
typename std::enable_if<
    futures::detail::callableWith<F, exception_wrapper>::value &&
        futures::detail::Extract<F>::ReturnsFuture::value,
    SemiFuture<T>>::type
SemiFuture<T>::deferError(F&& func) {
  return std::move(*this).defer(
      [func = std::forward<F>(func)](Try<T> t) mutable {
        if (t.hasException()) {
          return func(std::move(t.exception()));
        } else {
          return makeSemiFuture<T>(std::move(t));
        }
      });
}

template <class T>
Future<T> Future<T>::makeEmpty() {
  return Future<T>(futures::detail::EmptyConstruct{});
}

template <class T>
Future<T>::Future(Future<T>&& other) noexcept
    : futures::detail::FutureBase<T>(std::move(other)) {}

template <class T>
Future<T>& Future<T>::operator=(Future<T>&& other) noexcept {
  this->assign(other);
  return *this;
}

template <class T>
template <
    class T2,
    typename std::enable_if<
        !std::is_same<T, typename std::decay<T2>::type>::value &&
            std::is_constructible<T, T2&&>::value &&
            std::is_convertible<T2&&, T>::value,
        int>::type>
Future<T>::Future(Future<T2>&& other)
    : Future(std::move(other).then([](T2&& v) { return T(std::move(v)); })) {}

template <class T>
template <
    class T2,
    typename std::enable_if<
        !std::is_same<T, typename std::decay<T2>::type>::value &&
            std::is_constructible<T, T2&&>::value &&
            !std::is_convertible<T2&&, T>::value,
        int>::type>
Future<T>::Future(Future<T2>&& other)
    : Future(std::move(other).then([](T2&& v) { return T(std::move(v)); })) {}

template <class T>
template <
    class T2,
    typename std::enable_if<
        !std::is_same<T, typename std::decay<T2>::type>::value &&
            std::is_constructible<T, T2&&>::value,
        int>::type>
Future<T>& Future<T>::operator=(Future<T2>&& other) {
  return operator=(
      std::move(other).then([](T2&& v) { return T(std::move(v)); }));
}

// unwrap

template <class T>
template <class F>
typename std::
    enable_if<isFuture<F>::value, Future<typename isFuture<T>::Inner>>::type
    Future<T>::unwrap() {
  return then([](Future<typename isFuture<T>::Inner> internal_future) {
    return internal_future;
  });
}

template <class T>
inline Future<T> Future<T>::via(Executor* executor, int8_t priority) && {
  this->throwIfInvalid();

  this->setExecutor(executor, priority);

  auto newFuture = Future<T>(this->core_);
  this->core_ = nullptr;
  return newFuture;
}

template <class T>
inline Future<T> Future<T>::via(Executor* executor, int8_t priority) & {
  this->throwIfInvalid();
  Promise<T> p;
  auto sf = p.getSemiFuture();
  auto func = [p = std::move(p)](Try<T>&& t) mutable {
    p.setTry(std::move(t));
  };
  using R = futures::detail::callableResult<T, decltype(func)>;
  this->template thenImplementation<decltype(func), R>(
      std::move(func), typename R::Arg());
  // Construct future from semifuture manually because this may not have
  // an executor set due to legacy code. This means we can bypass the executor
  // check in SemiFuture::via
  auto f = Future<T>(sf.core_);
  sf.core_ = nullptr;
  return std::move(f).via(executor, priority);
}

template <typename T>
template <typename R, typename Caller, typename... Args>
  Future<typename isFuture<R>::Inner>
Future<T>::then(R(Caller::*func)(Args...), Caller *instance) {
  typedef typename std::remove_cv<typename std::remove_reference<
      typename futures::detail::ArgType<Args...>::FirstArg>::type>::type
      FirstArg;

  return then([instance, func](Try<T>&& t){
    return (instance->*func)(t.template get<isTry<FirstArg>::value, Args>()...);
  });
}

template <class T>
Future<Unit> Future<T>::then() {
  return then([] () {});
}

// onError where the callback returns T
template <class T>
template <class F>
typename std::enable_if<
    !futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::callableWith<F, exception_wrapper&>::value &&
        !futures::detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
Future<T>::onError(F&& func) {
  typedef std::remove_reference_t<
      typename futures::detail::Extract<F>::FirstArg>
      Exn;
  static_assert(
      std::is_same<typename futures::detail::Extract<F>::RawReturn, T>::value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  p.core_->setInterruptHandlerNoLock(this->core_->getInterruptHandler());
  auto sf = p.getSemiFuture();

  this->setCallback_(
      [state = futures::detail::makeCoreCallbackState(
           std::move(p), std::forward<F>(func))](Try<T>&& t) mutable {
        if (auto e = t.template tryGetExceptionObject<Exn>()) {
          state.setTry(makeTryWith([&] { return state.invoke(*e); }));
        } else {
          state.setTry(std::move(t));
        }
      });

  // Allow for applying to future with null executor while this is still
  // possible.
  // TODO(T26801487): Should have an executor
  return std::move(sf).via(&folly::InlineExecutor::instance());
}

// onError where the callback returns Future<T>
template <class T>
template <class F>
typename std::enable_if<
    !futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::callableWith<F, exception_wrapper&>::value &&
        futures::detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename futures::detail::Extract<F>::Return, Future<T>>::
          value,
      "Return type of onError callback must be T or Future<T>");
  typedef std::remove_reference_t<
      typename futures::detail::Extract<F>::FirstArg>
      Exn;

  Promise<T> p;
  auto sf = p.getSemiFuture();

  this->setCallback_(
      [state = futures::detail::makeCoreCallbackState(
           std::move(p), std::forward<F>(func))](Try<T>&& t) mutable {
        if (auto e = t.template tryGetExceptionObject<Exn>()) {
          auto tf2 = state.tryInvoke(*e);
          if (tf2.hasException()) {
            state.setException(std::move(tf2.exception()));
          } else {
            tf2->setCallback_([p = state.stealPromise()](Try<T> && t3) mutable {
              p.setTry(std::move(t3));
            });
          }
        } else {
          state.setTry(std::move(t));
        }
      });

  // Allow for applying to future with null executor while this is still
  // possible.
  // TODO(T26801487): Should have an executor
  return std::move(sf).via(&folly::InlineExecutor::instance());
}

template <class T>
template <class F>
Future<T> Future<T>::ensure(F&& func) {
  return this->then([funcw = std::forward<F>(func)](Try<T> && t) mutable {
    std::move(funcw)();
    return makeFuture(std::move(t));
  });
}

template <class T>
template <class F>
Future<T> Future<T>::onTimeout(Duration dur, F&& func, Timekeeper* tk) {
  return within(dur, tk).onError([funcw = std::forward<F>(func)](
      TimedOut const&) { return std::move(funcw)(); });
}

template <class T>
template <class F>
typename std::enable_if<
    futures::detail::callableWith<F, exception_wrapper>::value &&
        futures::detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename futures::detail::Extract<F>::Return, Future<T>>::
          value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  auto sf = p.getSemiFuture();
  this->setCallback_(
      [state = futures::detail::makeCoreCallbackState(
           std::move(p), std::forward<F>(func))](Try<T> t) mutable {
        if (t.hasException()) {
          auto tf2 = state.tryInvoke(std::move(t.exception()));
          if (tf2.hasException()) {
            state.setException(std::move(tf2.exception()));
          } else {
            tf2->setCallback_([p = state.stealPromise()](Try<T> && t3) mutable {
              p.setTry(std::move(t3));
            });
          }
        } else {
          state.setTry(std::move(t));
        }
      });

  // Allow for applying to future with null executor while this is still
  // possible.
  // TODO(T26801487): Should have an executor
  return std::move(sf).via(&folly::InlineExecutor::instance());
}

// onError(exception_wrapper) that returns T
template <class T>
template <class F>
typename std::enable_if<
    futures::detail::callableWith<F, exception_wrapper>::value &&
        !futures::detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename futures::detail::Extract<F>::Return, Future<T>>::
          value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  auto sf = p.getSemiFuture();
  this->setCallback_(
      [state = futures::detail::makeCoreCallbackState(
           std::move(p), std::forward<F>(func))](Try<T>&& t) mutable {
        if (t.hasException()) {
          state.setTry(makeTryWith(
              [&] { return state.invoke(std::move(t.exception())); }));
        } else {
          state.setTry(std::move(t));
        }
      });

  // Allow for applying to future with null executor while this is still
  // possible.
  // TODO(T26801487): Should have an executor
  return std::move(sf).via(&folly::InlineExecutor::instance());
}

template <class Func>
auto via(Executor* x, Func&& func)
    -> Future<typename isFuture<decltype(std::declval<Func>()())>::Inner> {
  // TODO make this actually more performant. :-P #7260175
  return via(x).then(std::forward<Func>(func));
}

// makeFuture

template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t) {
  return makeFuture(Try<typename std::decay<T>::type>(std::forward<T>(t)));
}

inline Future<Unit> makeFuture() {
  return makeFuture(Unit{});
}

// makeFutureWith(Future<T>()) -> Future<T>
template <class F>
typename std::enable_if<isFuture<typename std::result_of<F()>::type>::value,
                        typename std::result_of<F()>::type>::type
makeFutureWith(F&& func) {
  using InnerType =
      typename isFuture<typename std::result_of<F()>::type>::Inner;
  try {
    return std::forward<F>(func)();
  } catch (std::exception& e) {
    return makeFuture<InnerType>(
        exception_wrapper(std::current_exception(), e));
  } catch (...) {
    return makeFuture<InnerType>(exception_wrapper(std::current_exception()));
  }
}

// makeFutureWith(T()) -> Future<T>
// makeFutureWith(void()) -> Future<Unit>
template <class F>
typename std::enable_if<
    !(isFuture<typename std::result_of<F()>::type>::value),
    Future<Unit::LiftT<typename std::result_of<F()>::type>>>::type
makeFutureWith(F&& func) {
  using LiftedResult = Unit::LiftT<typename std::result_of<F()>::type>;
  return makeFuture<LiftedResult>(
      makeTryWith([&func]() mutable { return std::forward<F>(func)(); }));
}

template <class T>
Future<T> makeFuture(std::exception_ptr const& e) {
  return makeFuture(Try<T>(e));
}

template <class T>
Future<T> makeFuture(exception_wrapper ew) {
  return makeFuture(Try<T>(std::move(ew)));
}

template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value,
                        Future<T>>::type
makeFuture(E const& e) {
  return makeFuture(Try<T>(make_exception_wrapper<E>(e)));
}

template <class T>
Future<T> makeFuture(Try<T>&& t) {
  return Future<T>(new futures::detail::Core<T>(std::move(t)));
}

// via
Future<Unit> via(Executor* executor, int8_t priority) {
  return makeFuture().via(executor, priority);
}

// mapSetCallback calls func(i, Try<T>) when every future completes

template <class T, class InputIterator, class F>
void mapSetCallback(InputIterator first, InputIterator last, F func) {
  for (size_t i = 0; first != last; ++first, ++i) {
    first->setCallback_([func, i](Try<T>&& t) {
      func(i, std::move(t));
    });
  }
}

// collectAll (variadic)

// TODO(T26439406): Make return SemiFuture
template <typename... Fs>
typename futures::detail::CollectAllVariadicContext<
    typename std::decay<Fs>::type::value_type...>::type
collectAll(Fs&&... fs) {
  auto ctx = std::make_shared<futures::detail::CollectAllVariadicContext<
      typename std::decay<Fs>::type::value_type...>>();
  futures::detail::collectVariadicHelper<
      futures::detail::CollectAllVariadicContext>(ctx, std::forward<Fs>(fs)...);
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collectAll (iterator)

// TODO(T26439406): Make return SemiFuture
template <class InputIterator>
Future<
  std::vector<
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAll(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  struct CollectAllContext {
    CollectAllContext(size_t n) : results(n) {}
    ~CollectAllContext() {
      p.setValue(std::move(results));
    }
    Promise<std::vector<Try<T>>> p;
    std::vector<Try<T>> results;
  };

  auto ctx =
      std::make_shared<CollectAllContext>(size_t(std::distance(first, last)));
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    ctx->results[i] = std::move(t);
  });
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collect (iterator)

namespace futures {
namespace detail {

template <typename T>
struct CollectContext {
  struct Nothing {
    explicit Nothing(int /* n */) {}
  };

  using Result = typename std::conditional<
    std::is_void<T>::value,
    void,
    std::vector<T>>::type;

  using InternalResult = typename std::conditional<
    std::is_void<T>::value,
    Nothing,
    std::vector<Optional<T>>>::type;

  explicit CollectContext(size_t n) : result(n) {
    finalResult.reserve(n);
  }
  ~CollectContext() {
    if (!threw.exchange(true)) {
      // map Optional<T> -> T
      std::transform(result.begin(), result.end(),
                     std::back_inserter(finalResult),
                     [](Optional<T>& o) { return std::move(o.value()); });
      p.setValue(std::move(finalResult));
    }
  }
  inline void setPartialResult(size_t i, Try<T>& t) {
    result[i] = std::move(t.value());
  }
  Promise<Result> p;
  InternalResult result;
  Result finalResult;
  std::atomic<bool> threw {false};
};

} // namespace detail
} // namespace futures

// TODO(T26439406): Make return SemiFuture
template <class InputIterator>
Future<typename futures::detail::CollectContext<typename std::iterator_traits<
    InputIterator>::value_type::value_type>::Result>
collect(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  auto ctx = std::make_shared<futures::detail::CollectContext<T>>(
      std::distance(first, last));
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    if (t.hasException()) {
       if (!ctx->threw.exchange(true)) {
         ctx->p.setException(std::move(t.exception()));
       }
     } else if (!ctx->threw) {
       ctx->setPartialResult(i, t);
     }
  });
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collect (variadic)

// TODO(T26439406): Make return SemiFuture
template <typename... Fs>
typename futures::detail::CollectVariadicContext<
    typename std::decay<Fs>::type::value_type...>::type
collect(Fs&&... fs) {
  auto ctx = std::make_shared<futures::detail::CollectVariadicContext<
      typename std::decay<Fs>::type::value_type...>>();
  futures::detail::collectVariadicHelper<
      futures::detail::CollectVariadicContext>(ctx, std::forward<Fs>(fs)...);
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collectAny (iterator)

// TODO(T26439406): Make return SemiFuture
template <class InputIterator>
Future<
  std::pair<size_t,
            Try<
              typename
              std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAny(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  struct CollectAnyContext {
    CollectAnyContext() {}
    Promise<std::pair<size_t, Try<T>>> p;
    std::atomic<bool> done {false};
  };

  auto ctx = std::make_shared<CollectAnyContext>();
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    if (!ctx->done.exchange(true)) {
      ctx->p.setValue(std::make_pair(i, std::move(t)));
    }
  });
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collectAnyWithoutException (iterator)

// TODO(T26439406): Make return SemiFuture
template <class InputIterator>
Future<std::pair<
    size_t,
    typename std::iterator_traits<InputIterator>::value_type::value_type>>
collectAnyWithoutException(InputIterator first, InputIterator last) {
  typedef
      typename std::iterator_traits<InputIterator>::value_type::value_type T;

  struct CollectAnyWithoutExceptionContext {
    CollectAnyWithoutExceptionContext(){}
    Promise<std::pair<size_t, T>> p;
    std::atomic<bool> done{false};
    std::atomic<size_t> nFulfilled{0};
    size_t nTotal;
  };

  auto ctx = std::make_shared<CollectAnyWithoutExceptionContext>();
  ctx->nTotal = size_t(std::distance(first, last));

  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    if (!t.hasException() && !ctx->done.exchange(true)) {
      ctx->p.setValue(std::make_pair(i, std::move(t.value())));
    } else if (++ctx->nFulfilled == ctx->nTotal) {
      ctx->p.setException(t.exception());
    }
  });
  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// collectN (iterator)

// TODO(T26439406): Make return SemiFuture
template <class InputIterator>
Future<std::vector<std::pair<
    size_t,
    Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>>
collectN(InputIterator first, InputIterator last, size_t n) {
  using T =
      typename std::iterator_traits<InputIterator>::value_type::value_type;
  using V = std::vector<Optional<Try<T>>>;
  using Result = std::vector<std::pair<size_t, Try<T>>>;

  assert(n > 0);
  assert(std::distance(first, last) >= 0);

  struct CollectNContext {
    explicit CollectNContext(size_t numFutures) : v(numFutures) {}

    V v;
    std::atomic<size_t> completed = {0};
    Promise<Result> p;

    inline void setPartialResult(size_t index, Try<T>&& t) {
      v[index] = std::move(t);
    }

    inline void complete() {
      Result result;
      result.reserve(completed.load());
      for (size_t i = 0; i < v.size(); ++i) {
        auto& entry = v[i];
        if (entry.hasValue()) {
          result.emplace_back(i, std::move(entry).value());
        }
      }
      p.setTry(Try<Result>(std::move(result)));
    }
  };

  auto numFutures = static_cast<size_t>(std::distance(first, last));
  auto ctx = std::make_shared<CollectNContext>(numFutures);

  if (numFutures < n) {
    ctx->p.setException(std::runtime_error("Not enough futures"));
  } else {
    // for each completed Future, increase count and add to vector, until we
    // have n completed futures at which point we fulfil our Promise with the
    // vector
    mapSetCallback<T>(first, last, [ctx, n](size_t i, Try<T>&& t) {
      auto c = ++ctx->completed;
      if (c <= n) {
        ctx->setPartialResult(i, std::move(t));
        if (c == n) {
          ctx->complete();
        }
      }
    });
  }

  return ctx->p.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// reduce (iterator)

template <class It, class T, class F>
Future<T> reduce(It first, It last, T&& initial, F&& func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef typename std::iterator_traits<It>::value_type::value_type ItT;
  typedef typename std::conditional<
      futures::detail::callableWith<F, T&&, Try<ItT>&&>::value,
      Try<ItT>,
      ItT>::type Arg;
  typedef isTry<Arg> IsTry;

  auto sfunc = std::make_shared<F>(std::move(func));

  auto f = first->then(
      [ minitial = std::move(initial), sfunc ](Try<ItT> & head) mutable {
        return (*sfunc)(
            std::move(minitial), head.template get<IsTry::value, Arg&&>());
      });

  for (++first; first != last; ++first) {
    f = collectAll(f, *first).then([sfunc](std::tuple<Try<T>, Try<ItT>>& t) {
      return (*sfunc)(std::move(std::get<0>(t).value()),
                  // Either return a ItT&& or a Try<ItT>&& depending
                  // on the type of the argument of func.
                  std::get<1>(t).template get<IsTry::value, Arg&&>());
    });
  }

  return f;
}

// window (collection)

template <class Collection, class F, class ItT, class Result>
std::vector<Future<Result>>
window(Collection input, F func, size_t n) {
  // Use global inline executor singleton
  auto executor = &InlineExecutor::instance();
  return window(executor, std::move(input), std::move(func), n);
}

template <class Collection, class F, class ItT, class Result>
std::vector<Future<Result>>
window(Executor* executor, Collection input, F func, size_t n) {
  struct WindowContext {
    WindowContext(Executor* executor_, Collection&& input_, F&& func_)
        : executor(executor_),
          input(std::move(input_)),
          promises(input.size()),
          func(std::move(func_)) {}
    std::atomic<size_t> i{0};
    Executor* executor;
    Collection input;
    std::vector<Promise<Result>> promises;
    F func;

    static inline void spawn(std::shared_ptr<WindowContext> ctx) {
      size_t i = ctx->i++;
      if (i < ctx->input.size()) {
        auto fut =
            makeFutureWith([&] { return ctx->func(std::move(ctx->input[i])); });
        fut.setCallback_([ctx = std::move(ctx), i](Try<Result>&& t) mutable {
          const auto executor_ = ctx->executor;
          executor_->add([ctx = std::move(ctx), i, t = std::move(t)]() mutable {
            ctx->promises[i].setTry(std::move(t));
            // Chain another future onto this one
            spawn(std::move(ctx));
          });
        });
      }
    }
  };

  auto max = std::min(n, input.size());

  auto ctx = std::make_shared<WindowContext>(
      executor, std::move(input), std::move(func));

  // Start the first n Futures
  for (size_t i = 0; i < max; ++i) {
    executor->add([ctx]() mutable { WindowContext::spawn(std::move(ctx)); });
  }

  std::vector<Future<Result>> futures;
  futures.reserve(ctx->promises.size());
  for (auto& promise : ctx->promises) {
    futures.emplace_back(promise.getSemiFuture().via(executor));
  }

  return futures;
}

// reduce

template <class T>
template <class I, class F>
Future<I> Future<T>::reduce(I&& initial, F&& func) {
  return then([
    minitial = std::forward<I>(initial),
    mfunc = std::forward<F>(func)
  ](T& vals) mutable {
    auto ret = std::move(minitial);
    for (auto& val : vals) {
      ret = mfunc(std::move(ret), std::move(val));
    }
    return ret;
  });
}

// unorderedReduce (iterator)

// TODO(T26439406): Make return SemiFuture
template <class It, class T, class F, class ItT, class Arg>
Future<T> unorderedReduce(It first, It last, T initial, F func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef isTry<Arg> IsTry;

  struct UnorderedReduceContext {
    UnorderedReduceContext(T&& memo, F&& fn, size_t n)
        : lock_(), memo_(makeFuture<T>(std::move(memo))),
          func_(std::move(fn)), numThens_(0), numFutures_(n), promise_()
      {}
    folly::MicroSpinLock lock_; // protects memo_ and numThens_
    Future<T> memo_;
    F func_;
    size_t numThens_; // how many Futures completed and called .then()
    size_t numFutures_; // how many Futures in total
    Promise<T> promise_;
  };

  auto ctx = std::make_shared<UnorderedReduceContext>(
    std::move(initial), std::move(func), std::distance(first, last));

  mapSetCallback<ItT>(
      first,
      last,
      [ctx](size_t /* i */, Try<ItT>&& t) {
        // Futures can be completed in any order, simultaneously.
        // To make this non-blocking, we create a new Future chain in
        // the order of completion to reduce the values.
        // The spinlock just protects chaining a new Future, not actually
        // executing the reduce, which should be really fast.
        folly::MSLGuard lock(ctx->lock_);
        ctx->memo_ =
            ctx->memo_.then([ ctx, mt = std::move(t) ](T && v) mutable {
              // Either return a ItT&& or a Try<ItT>&& depending
              // on the type of the argument of func.
              return ctx->func_(std::move(v),
                                mt.template get<IsTry::value, Arg&&>());
            });
        if (++ctx->numThens_ == ctx->numFutures_) {
          // After reducing the value of the last Future, fulfill the Promise
          ctx->memo_.setCallback_(
              [ctx](Try<T>&& t2) { ctx->promise_.setValue(std::move(t2)); });
        }
      });

  return ctx->promise_.getSemiFuture().via(&folly::InlineExecutor::instance());
}

// within

template <class T>
Future<T> Future<T>::within(Duration dur, Timekeeper* tk) {
  return within(dur, TimedOut(), tk);
}

template <class T>
template <class E>
Future<T> Future<T>::within(Duration dur, E e, Timekeeper* tk) {

  struct Context {
    Context(E ex) : exception(std::move(ex)), promise() {}
    E exception;
    Future<Unit> thisFuture;
    Promise<T> promise;
    std::atomic<bool> token {false};
  };

  if (this->isReady()) {
    return std::move(*this);
  }

  std::shared_ptr<Timekeeper> tks;
  if (LIKELY(!tk)) {
    tks = folly::detail::getTimekeeperSingleton();
    tk = tks.get();
  }

  if (UNLIKELY(!tk)) {
    return makeFuture<T>(NoTimekeeper());
  }

  auto ctx = std::make_shared<Context>(std::move(e));

  ctx->thisFuture = this->then([ctx](Try<T>&& t) mutable {
    if (ctx->token.exchange(true) == false) {
      ctx->promise.setTry(std::move(t));
    }
  });

  // Properly propagate interrupt values through futures chained after within()
  ctx->promise.setInterruptHandler(
      [weakCtx = to_weak_ptr(ctx)](const exception_wrapper& ex) {
        if (auto lockedCtx = weakCtx.lock()) {
          lockedCtx->thisFuture.raise(ex);
        }
      });

  // Have time keeper use a weak ptr to hold ctx,
  // so that ctx can be deallocated as soon as the future job finished.
  tk->after(dur).then([weakCtx = to_weak_ptr(ctx)](Try<Unit> const& t) mutable {
    auto lockedCtx = weakCtx.lock();
    if (!lockedCtx) {
      // ctx already released. "this" completed first, cancel "after"
      return;
    }
    // "after" completed first, cancel "this"
    lockedCtx->thisFuture.raise(TimedOut());
    if (lockedCtx->token.exchange(true) == false) {
      if (t.hasException()) {
        lockedCtx->promise.setException(std::move(t.exception()));
      } else {
        lockedCtx->promise.setException(std::move(lockedCtx->exception));
      }
    }
  });

  auto* currentExecutor = this->getExecutor();
  return ctx->promise.getSemiFuture().via(
      currentExecutor ? currentExecutor : &folly::InlineExecutor::instance());
}

// delayed

template <class T>
Future<T> Future<T>::delayed(Duration dur, Timekeeper* tk) {
  return collectAll(*this, futures::sleep(dur, tk))
      .then([](std::tuple<Try<T>, Try<Unit>> tup) {
        Try<T>& t = std::get<0>(tup);
        return makeFuture<T>(std::move(t));
      });
}

namespace futures {
namespace detail {

template <class FutureType, typename T = typename FutureType::value_type>
void waitImpl(FutureType& f) {
  // short-circuit if there's nothing to do
  if (f.isReady()) {
    return;
  }

  FutureBatonType baton;
  f.setCallback_([&](const Try<T>& /* t */) { baton.post(); });
  baton.wait();
  assert(f.isReady());
}

template <class T>
void convertFuture(SemiFuture<T>&& sf, Future<T>& f) {
  // Carry executor from f, inserting an inline executor if it did not have one
  auto* currentExecutor = f.getExecutor();
  f = std::move(sf).via(
      currentExecutor ? currentExecutor : &folly::InlineExecutor::instance());
}

template <class T>
void convertFuture(SemiFuture<T>&& sf, SemiFuture<T>& f) {
  f = std::move(sf);
}

template <class FutureType, typename T = typename FutureType::value_type>
void waitImpl(FutureType& f, Duration dur) {
  // short-circuit if there's nothing to do
  if (f.isReady()) {
    return;
  }

  Promise<T> promise;
  auto ret = promise.getSemiFuture();
  auto baton = std::make_shared<FutureBatonType>();
  f.setCallback_([baton, promise = std::move(promise)](Try<T>&& t) mutable {
    promise.setTry(std::move(t));
    baton->post();
  });
  convertFuture(std::move(ret), f);
  if (baton->try_wait_for(dur)) {
    assert(f.isReady());
  }
}

template <class T>
void waitViaImpl(Future<T>& f, DrivableExecutor* e) {
  // Set callback so to ensure that the via executor has something on it
  // so that once the preceding future triggers this callback, drive will
  // always have a callback to satisfy it
  if (f.isReady()) {
    return;
  }
  f = std::move(f).via(e).then([](T&& t) { return std::move(t); });
  while (!f.isReady()) {
    e->drive();
  }
  assert(f.isReady());
}

template <class T, typename Rep, typename Period>
void waitViaImpl(
    Future<T>& f,
    TimedDrivableExecutor* e,
    const std::chrono::duration<Rep, Period>& timeout) {
  // Set callback so to ensure that the via executor has something on it
  // so that once the preceding future triggers this callback, drive will
  // always have a callback to satisfy it
  if (f.isReady()) {
    return;
  }
  // Chain operations, ensuring that the executor is kept alive for the duration
  f = std::move(f).via(e).then(
      [keepAlive = e->getKeepAliveToken()](T&& t) { return std::move(t); });
  auto now = std::chrono::steady_clock::now();
  auto deadline = now + timeout;
  while (!f.isReady() && (now < deadline)) {
    e->try_drive_until(deadline);
    now = std::chrono::steady_clock::now();
  }
  assert(f.isReady() || (now >= deadline));
}

} // namespace detail
} // namespace futures

template <class T>
SemiFuture<T>& SemiFuture<T>::wait() & {
  if (auto deferredExecutor = getDeferredExecutor()) {
    deferredExecutor->wait();
    deferredExecutor->runAndDestroy();
    this->core_->setExecutor(nullptr);
  } else {
    futures::detail::waitImpl(*this);
  }
  return *this;
}

template <class T>
SemiFuture<T>&& SemiFuture<T>::wait() && {
  return std::move(wait());
}

template <class T>
SemiFuture<T>& SemiFuture<T>::wait(Duration dur) & {
  if (auto deferredExecutor = getDeferredExecutor()) {
    if (deferredExecutor->wait(dur)) {
      deferredExecutor->runAndDestroy();
      this->core_->setExecutor(nullptr);
    }
  } else {
    futures::detail::waitImpl(*this, dur);
  }
  return *this;
}

template <class T>
SemiFuture<T>&& SemiFuture<T>::wait(Duration dur) && {
  return std::move(wait(dur));
}

template <class T>
T SemiFuture<T>::get() && {
  return std::move(*this).getTry().value();
}

template <class T>
T SemiFuture<T>::get(Duration dur) && {
  return std::move(*this).getTry(dur).value();
}

template <class T>
Try<T> SemiFuture<T>::getTry() && {
  wait();
  auto future = folly::Future<T>(this->core_);
  this->core_ = nullptr;
  return std::move(std::move(future).getTry());
}

template <class T>
Try<T> SemiFuture<T>::getTry(Duration dur) && {
  wait(dur);
  if (auto deferredExecutor = getDeferredExecutor()) {
    deferredExecutor->detach();
  }
  this->core_->setExecutor(nullptr);
  auto future = folly::Future<T>(this->core_);
  this->core_ = nullptr;

  if (!future.isReady()) {
    throwTimedOut();
  }
  return std::move(std::move(future).getTry());
}

template <class T>
Future<T>& Future<T>::wait() & {
  futures::detail::waitImpl(*this);
  return *this;
}

template <class T>
Future<T>&& Future<T>::wait() && {
  futures::detail::waitImpl(*this);
  return std::move(*this);
}

template <class T>
Future<T>& Future<T>::wait(Duration dur) & {
  futures::detail::waitImpl(*this, dur);
  return *this;
}

template <class T>
Future<T>&& Future<T>::wait(Duration dur) && {
  futures::detail::waitImpl(*this, dur);
  return std::move(*this);
}

template <class T>
Future<T>& Future<T>::waitVia(DrivableExecutor* e) & {
  futures::detail::waitViaImpl(*this, e);
  return *this;
}

template <class T>
Future<T>&& Future<T>::waitVia(DrivableExecutor* e) && {
  futures::detail::waitViaImpl(*this, e);
  return std::move(*this);
}

template <class T>
Future<T>& Future<T>::waitVia(TimedDrivableExecutor* e, Duration dur) & {
  futures::detail::waitViaImpl(*this, e, dur);
  return *this;
}

template <class T>
Future<T>&& Future<T>::waitVia(TimedDrivableExecutor* e, Duration dur) && {
  futures::detail::waitViaImpl(*this, e, dur);
  return std::move(*this);
}

template <class T>
T Future<T>::get() {
  return std::move(wait().value());
}

template <class T>
T Future<T>::get(Duration dur) {
  wait(dur);
  if (!this->isReady()) {
    throwTimedOut();
  }
  return std::move(this->value());
}

template <class T>
Try<T>& Future<T>::getTry() {
  return result();
}

template <class T>
T Future<T>::getVia(DrivableExecutor* e) {
  return std::move(waitVia(e).value());
}

template <class T>
T Future<T>::getVia(TimedDrivableExecutor* e, Duration dur) {
  waitVia(e, dur);
  if (!this->isReady()) {
    throwTimedOut();
  }
  return std::move(value());
}

template <class T>
Try<T>& Future<T>::getTryVia(DrivableExecutor* e) {
  return waitVia(e).getTry();
}

template <class T>
Try<T>& Future<T>::getTryVia(TimedDrivableExecutor* e, Duration dur) {
  waitVia(e, dur);
  if (!this->isReady()) {
    throwTimedOut();
  }
  return result();
}

namespace futures {
namespace detail {
template <class T>
struct TryEquals {
  static bool equals(const Try<T>& t1, const Try<T>& t2) {
    return t1.value() == t2.value();
  }
};
} // namespace detail
} // namespace futures

template <class T>
Future<bool> Future<T>::willEqual(Future<T>& f) {
  return collectAll(*this, f).then([](const std::tuple<Try<T>, Try<T>>& t) {
    if (std::get<0>(t).hasValue() && std::get<1>(t).hasValue()) {
      return futures::detail::TryEquals<T>::equals(
          std::get<0>(t), std::get<1>(t));
    } else {
      return false;
    }
  });
}

template <class T>
template <class F>
Future<T> Future<T>::filter(F&& predicate) {
  return this->then([p = std::forward<F>(predicate)](T val) {
    T const& valConstRef = val;
    if (!p(valConstRef)) {
      throwPredicateDoesNotObtain();
    }
    return val;
  });
}

template <class F>
inline Future<Unit> when(bool p, F&& thunk) {
  return p ? std::forward<F>(thunk)().unit() : makeFuture();
}

template <class P, class F>
Future<Unit> whileDo(P&& predicate, F&& thunk) {
  if (predicate()) {
    auto future = thunk();
    return future.then([
      predicate = std::forward<P>(predicate),
      thunk = std::forward<F>(thunk)
    ]() mutable {
      return whileDo(std::forward<P>(predicate), std::forward<F>(thunk));
    });
  }
  return makeFuture();
}

template <class F>
Future<Unit> times(const int n, F&& thunk) {
  return folly::whileDo(
      [ n, count = std::make_unique<std::atomic<int>>(0) ]() mutable {
        return count->fetch_add(1) < n;
      },
      std::forward<F>(thunk));
}

namespace futures {
template <class It, class F, class ItT, class Result>
std::vector<Future<Result>> map(It first, It last, F func) {
  std::vector<Future<Result>> results;
  for (auto it = first; it != last; it++) {
    results.push_back(it->then(func));
  }
  return results;
}
} // namespace futures

// Instantiate the most common Future types to save compile time
extern template class Future<Unit>;
extern template class Future<bool>;
extern template class Future<int>;
extern template class Future<int64_t>;
extern template class Future<std::string>;
extern template class Future<double>;
} // namespace folly
