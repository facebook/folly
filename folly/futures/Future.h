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
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>
#if FOLLY_HAS_COROUTINES
#include <experimental/coroutine>
#endif

#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Try.h>
#include <folly/Unit.h>
#include <folly/Utility.h>
#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/TimedDrivableExecutor.h>
#include <folly/functional/Invoke.h>
#include <folly/futures/Promise.h>
#include <folly/futures/detail/Types.h>
#include <folly/lang/Exception.h>

// boring predeclarations and details
#include <folly/futures/Future-pre.h>

// not-boring helpers, e.g. all in folly::futures, makeFuture variants, etc.
// Needs to be included after Future-pre.h and before Future-inl.h
#include <folly/futures/helpers.h>

namespace folly {

class FOLLY_EXPORT FutureException : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

class FOLLY_EXPORT FutureInvalid : public FutureException {
 public:
  FutureInvalid() : FutureException("Future invalid") {}
};

class FOLLY_EXPORT FutureNotReady : public FutureException {
 public:
  FutureNotReady() : FutureException("Future not ready") {}
};

class FOLLY_EXPORT FutureCancellation : public FutureException {
 public:
  FutureCancellation() : FutureException("Future was cancelled") {}
};

class FOLLY_EXPORT FutureTimeout : public FutureException {
 public:
  FutureTimeout() : FutureException("Timed out") {}
};

class FOLLY_EXPORT FuturePredicateDoesNotObtain : public FutureException {
 public:
  FuturePredicateDoesNotObtain()
      : FutureException("Predicate does not obtain") {}
};

class FOLLY_EXPORT FutureNoTimekeeper : public FutureException {
 public:
  FutureNoTimekeeper() : FutureException("No timekeeper available") {}
};

class FOLLY_EXPORT FutureNoExecutor : public FutureException {
 public:
  FutureNoExecutor() : FutureException("No executor provided to via") {}
};

template <class T>
class Future;

template <class T>
class SemiFuture;

template <class T>
class FutureSplitter;

namespace futures {
namespace detail {
template <class T>
class FutureBase {
 public:
  typedef T value_type;

  /// Construct a Future from a value (perfect forwarding)
  template <
      class T2 = T,
      typename = typename std::enable_if<
          !isFuture<typename std::decay<T2>::type>::value &&
          !isSemiFuture<typename std::decay<T2>::type>::value>::type>
  /* implicit */ FutureBase(T2&& val);

  template <class T2 = T>
  /* implicit */ FutureBase(
      typename std::enable_if<std::is_same<Unit, T2>::value>::type*);

  template <
      class... Args,
      typename std::enable_if<std::is_constructible<T, Args&&...>::value, int>::
          type = 0>
  explicit FutureBase(in_place_t, Args&&... args);

  FutureBase(FutureBase<T> const&) = delete;
  FutureBase(SemiFuture<T>&&) noexcept;
  FutureBase(Future<T>&&) noexcept;

  // not copyable
  FutureBase(Future<T> const&) = delete;
  FutureBase(SemiFuture<T> const&) = delete;

  ~FutureBase();

  /// true if this has a shared state;
  /// false if this has been consumed/moved-out.
  bool valid() const noexcept {
    return core_ != nullptr;
  }

  /// Returns a reference to the result, with a reference category and const-
  /// qualification equivalent to the reference category and const-qualification
  /// of the receiver.
  ///
  /// If moved-from, throws FutureInvalid.
  ///
  /// If !isReady(), throws FutureNotReady.
  ///
  /// If an exception has been captured, throws that exception.
  T& value() &;
  T const& value() const&;
  T&& value() &&;
  T const&& value() const&&;

  /// Returns a reference to the try of the result. Throws as for value if
  /// future is not valid.
  Try<T>& result() &;
  Try<T> const& result() const&;
  Try<T>&& result() &&;
  Try<T> const&& result() const&&;

  /** True when the result (or exception) is ready. */
  bool isReady() const;

  /// sugar for getTry().hasValue()
  bool hasValue() const;

  /// sugar for getTry().hasException()
  bool hasException() const;

  /// If the promise has been fulfilled, return an Optional with the Try<T>.
  /// Otherwise return an empty Optional.
  /// Note that this moves the Try<T> out.
  Optional<Try<T>> poll();

  /// This is not the method you're looking for.
  ///
  /// This needs to be public because it's used by make* and when*, and it's
  /// not worth listing all those and their fancy template signatures as
  /// friends. But it's not for public consumption.
  template <class F>
  void setCallback_(F&& func);

  template <class E>
  void raise(E&& exception) {
    raise(make_exception_wrapper<typename std::remove_reference<E>::type>(
        std::forward<E>(exception)));
  }

  /// Raise an interrupt. If the promise holder has an interrupt
  /// handler it will be called and potentially stop asynchronous work from
  /// being done. This is advisory only - a promise holder may not set an
  /// interrupt handler, or may do anything including ignore. But, if you know
  /// your future supports this the most likely result is stopping or
  /// preventing the asynchronous operation (if in time), and the promise
  /// holder setting an exception on the future. (That may happen
  /// asynchronously, of course.)
  void raise(exception_wrapper interrupt);

  void cancel() {
    raise(FutureCancellation());
  }

 protected:
  friend class Promise<T>;
  template <class>
  friend class SemiFuture;
  template <class>
  friend class Future;

  using CoreType = futures::detail::Core<T>;
  using corePtr = CoreType*;

  // Throws FutureInvalid if there is no shared state object; else returns it
  // by ref.
  //
  // Implementation methods should usually use this instead of `this->core_`.
  // The latter should be used only when you need the possibly-null pointer.
  CoreType& getCore() {
    return getCoreImpl(*this);
  }
  CoreType const& getCore() const {
    return getCoreImpl(*this);
  }

  template <typename Self>
  static decltype(auto) getCoreImpl(Self& self) {
    if (!self.core_) {
      throw_exception<FutureInvalid>();
    }
    return *self.core_;
  }

  Try<T>& getCoreTryChecked() {
    return getCoreTryChecked(*this);
  }
  Try<T> const& getCoreTryChecked() const {
    return getCoreTryChecked(*this);
  }

  template <typename Self>
  static decltype(auto) getCoreTryChecked(Self& self) {
    auto& core = self.getCore();
    if (!core.hasResult()) {
      throw_exception<FutureNotReady>();
    }
    return core.getTry();
  }

  // shared core state object
  // usually you should use `getCore()` instead of directly accessing `core_`.
  corePtr core_;

  explicit FutureBase(corePtr obj) : core_(obj) {}

  explicit FutureBase(futures::detail::EmptyConstruct) noexcept;

  void detach();

  void throwIfInvalid() const;

  void assign(FutureBase<T>&& other) noexcept;

  Executor* getExecutor() const {
    return getCore().getExecutor();
  }

  void setExecutor(Executor* x, int8_t priority = Executor::MID_PRI) {
    getCore().setExecutor(x, priority);
  }

  void setExecutor(
      Executor::KeepAlive<> x,
      int8_t priority = Executor::MID_PRI) {
    getCore().setExecutor(std::move(x), priority);
  }

  // Variant: returns a value
  // e.g. f.then([](Try<T> t){ return t.value(); });
  template <typename F, typename R, bool isTry, typename... Args>
  typename std::enable_if<!R::ReturnsFuture::value, typename R::Return>::type
  thenImplementation(F&& func, futures::detail::argResult<isTry, F, Args...>);

  // Variant: returns a Future
  // e.g. f.then([](Try<T> t){ return makeFuture<T>(t); });
  template <typename F, typename R, bool isTry, typename... Args>
  typename std::enable_if<R::ReturnsFuture::value, typename R::Return>::type
  thenImplementation(F&& func, futures::detail::argResult<isTry, F, Args...>);
};
template <class T>
void convertFuture(SemiFuture<T>&& sf, Future<T>& f);
} // namespace detail
} // namespace futures

/// The interface (along with SemiFuture) for the consumer-side of a
///   producer/consumer pair.
///
/// Future vs. SemiFuture:
///
/// - The consumer-side should generally start with a SemiFuture, not a Future.
/// - Example, when a library creates and returns a future, it should usually
///   return a `SemiFuture`, not a Future.
/// - Reason: so the thread policy for continuations (`.then()`, etc.) can be
///   specified by the library's caller (using `.via()`).
/// - A SemiFuture is converted to a Future using `.via()`.
/// - Use `makePromiseContract()` when creating both a Promise and an associated
///   SemiFuture/Future.
///
/// When practical, prefer SemiFuture/Future's nonblocking style/pattern:
///
/// - the nonblocking style uses continuations, e.g., `.then()`, etc.; the
///   continuations are deferred until the result is available.
/// - the blocking style blocks until complete, e.g., `.wait()`, `.get()`, etc.
/// - the two styles cannot be mixed within the same future; use one or the
///   other.
///
/// SemiFuture/Future also provide a back-channel so an interruption request can
///   be sent from consumer to producer; see SemiFuture/Future's `raise()`
///   and Promise's `setInterruptHandler()`.
///
/// The consumer-side SemiFuture/Future objects should generally be accessed
///   via a single thread. That thread is referred to as the 'consumer thread.'
template <class T>
class SemiFuture : private futures::detail::FutureBase<T> {
 private:
  using Base = futures::detail::FutureBase<T>;
  using DeferredExecutor = futures::detail::DeferredExecutor;
  using TimePoint = std::chrono::system_clock::time_point;

 public:
  ~SemiFuture();

  static SemiFuture<T> makeEmpty(); // equivalent to moved-from

  // Export public interface of FutureBase
  // FutureBase is inherited privately to avoid subclasses being cast to
  // a FutureBase pointer
  using typename Base::value_type;

  /// Construct a Future from a value (perfect forwarding)
  template <
      class T2 = T,
      typename = typename std::enable_if<
          !isFuture<typename std::decay<T2>::type>::value &&
          !isSemiFuture<typename std::decay<T2>::type>::value>::type>
  /* implicit */ SemiFuture(T2&& val) : Base(std::forward<T2>(val)) {}

  template <class T2 = T>
  /* implicit */ SemiFuture(
      typename std::enable_if<std::is_same<Unit, T2>::value>::type* p = nullptr)
      : Base(p) {}

  template <
      class... Args,
      typename std::enable_if<std::is_constructible<T, Args&&...>::value, int>::
          type = 0>
  explicit SemiFuture(in_place_t, Args&&... args)
      : Base(in_place, std::forward<Args>(args)...) {}

  SemiFuture(SemiFuture<T> const&) = delete;
  // movable
  SemiFuture(SemiFuture<T>&&) noexcept;
  // safe move-constructabilty from Future
  /* implicit */ SemiFuture(Future<T>&&) noexcept;

  using Base::cancel;
  using Base::hasException;
  using Base::hasValue;
  using Base::isReady;
  using Base::poll;
  using Base::raise;
  using Base::result;
  using Base::setCallback_;
  using Base::valid;
  using Base::value;

  SemiFuture& operator=(SemiFuture const&) = delete;
  SemiFuture& operator=(SemiFuture&&) noexcept;
  SemiFuture& operator=(Future<T>&&) noexcept;

  /// Block until the future is fulfilled. Returns the value (moved out), or
  /// throws the exception. The future must not already have a callback.
  T get() &&;

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// value (moved out), or throws the exception (which might be a FutureTimeout
  /// exception).
  T get(Duration dur) &&;

  /// Block until the future is fulfilled. Returns the Try of the value (moved
  /// out).
  Try<T> getTry() &&;

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// Try of the value (moved out) or may throw a FutureTimeout exception.
  Try<T> getTry(Duration dur) &&;

  /// Block until this Future is complete. Returns a reference to this Future.
  SemiFuture<T>& wait() &;

  /// Overload of wait() for rvalue Futures
  SemiFuture<T>&& wait() &&;

  /// Block until this Future is complete or until the given Duration passes.
  /// Returns a reference to this Future
  SemiFuture<T>& wait(Duration) &;

  /// Overload of wait(Duration) for rvalue Futures
  SemiFuture<T>&& wait(Duration) &&;

  /// Returns a Future which will call back on the other side of executor.
  ///
  /// The ref-qualifier allows for `this` to be moved out so we
  /// don't get access-after-free situations in chaining.
  /// https://akrzemi1.wordpress.com/2014/06/02/ref-qualifiers/
  Future<T> via(Executor* executor, int8_t priority = Executor::MID_PRI) &&;

  Future<T> via(
      Executor::KeepAlive<> executor,
      int8_t priority = Executor::MID_PRI) &&;

  /**
   * Defer work to run on the consumer of the future.
   * Function must take a Try as a parameter.
   * This work will be run either on an executor that the caller sets on the
   * SemiFuture, or inline with the call to .get().
   * NB: This is a custom method because boost-blocking executors is a
   * special-case for work deferral in folly. With more general boost-blocking
   * support all executors would boost block and we would simply use some form
   * of driveable executor here.
   */
  template <typename F>
  SemiFuture<typename futures::detail::tryCallableResult<T, F>::value_type>
  defer(F&& func) &&;

  template <typename R, typename... Args>
  auto defer(R (&func)(Args...)) && {
    return std::move(*this).defer(&func);
  }

  /**
   * Defer for functions taking a T rather than a Try<T>.
   */
  template <typename F>
  SemiFuture<typename futures::detail::valueCallableResult<T, F>::value_type>
  deferValue(F&& func) &&;

  template <typename R, typename... Args>
  auto deferValue(R (&func)(Args...)) && {
    return std::move(*this).deferValue(&func);
  }

  /// Set an error continuation for this SemiFuture. The continuation should
  /// take a single argument of the type that you want to catch, and should
  /// return a `T`, `Future<T>` or `SemiFuture<`T`> (see overload below).
  /// For instance,
  ///
  /// makeSemiFuture()
  ///   .defer([] {
  ///     throw std::runtime_error("oh no!");
  ///     return 42;
  ///   })
  ///   .deferError<std::runtime_error>([] (auto const& e) {
  ///     LOG(INFO) << "std::runtime_error: " << e.what();
  ///     return -1; // or makeSemiFuture<int>(-1)
  ///   });
  /// Overload of deferError where continuation can be called with a known
  /// exception type and returns T, Future<T> or SemiFuture<T>
  template <class ExceptionType, class F>
  SemiFuture<T> deferError(F&& func) &&;

  template <class ExceptionType, class R, class... Args>
  SemiFuture<T> deferError(R (&func)(Args...)) && {
    return std::move(*this).template deferError<ExceptionType>(&func);
  }

  /// Overload of deferError where continuation can be called with
  /// exception_wrapper&& and returns T, Future<T> or SemiFuture<T>
  template <class F>
  SemiFuture<T> deferError(F&& func) &&;

  template <class R, class... Args>
  SemiFuture<T> deferError(R (&func)(Args...)) && {
    return std::move(*this).deferError(&func);
  }

  /// Return a future that completes inline, as if the future had no executor.
  /// Intended for porting legacy code without behavioural change, and for rare
  /// cases where this is really the intended behaviour.
  /// Future is unsafe in the sense that the executor it completes on is
  /// non-deterministic in the standard case.
  /// For new code, or to update code that temporarily uses this, please
  /// use via and pass a meaningful executor.
  Future<T> toUnsafeFuture() &&;

#if FOLLY_HAS_COROUTINES
  class promise_type {
   public:
    SemiFuture get_return_object() {
      return promise_.getSemiFuture();
    }

    std::experimental::suspend_never initial_suspend() {
      return {};
    }

    std::experimental::suspend_never final_suspend() {
      return {};
    }

    void return_value(T& value) {
      promise_.setValue(std::move(value));
    }

    void unhandled_exception() {
      promise_.setException(exception_wrapper(std::current_exception()));
    }

   private:
    folly::Promise<T> promise_;
  };

  template <typename Awaitable>
  static SemiFuture fromAwaitable(Awaitable&& awaitable) {
    return [](Awaitable awaitable) -> SemiFuture {
      co_return co_await awaitable;
    }(std::forward<Awaitable>(awaitable));
  }
#endif

 private:
  friend class Promise<T>;
  template <class>
  friend class futures::detail::FutureBase;
  template <class>
  friend class SemiFuture;
  template <class>
  friend class Future;

  using typename Base::corePtr;
  using Base::setExecutor;
  using Base::throwIfInvalid;

  template <class T2>
  friend SemiFuture<T2> makeSemiFuture(Try<T2>&&);

  explicit SemiFuture(corePtr obj) : Base(obj) {}

  explicit SemiFuture(futures::detail::EmptyConstruct) noexcept
      : Base(futures::detail::EmptyConstruct{}) {}

  DeferredExecutor* getDeferredExecutor() const;

  static void releaseDeferredExecutor(corePtr core);
};

template <class T>
std::pair<Promise<T>, SemiFuture<T>> makePromiseContract() {
  auto p = Promise<T>();
  auto f = p.getSemiFuture();
  return std::make_pair(std::move(p), std::move(f));
}

/// The interface (along with SemiFuture) for the consumer-side of a
///   producer/consumer pair.
///
/// Future vs. SemiFuture:
///
/// - The consumer-side should generally start with a SemiFuture, not a Future.
/// - Example, when a library creates and returns a future, it should usually
///   return a `SemiFuture`, not a Future.
/// - Reason: so the thread policy for continuations (`.then()`, etc.) can be
///   specified by the library's caller (using `.via()`).
/// - A SemiFuture is converted to a Future using `.via()`.
/// - Use `makePromiseContract()` when creating both a Promise and an associated
///   SemiFuture/Future.
///
/// When practical, prefer SemiFuture/Future's nonblocking style/pattern:
///
/// - the nonblocking style uses continuations, e.g., `.then()`, etc.; the
///   continuations are deferred until the result is available.
/// - the blocking style blocks until complete, e.g., `.wait()`, `.get()`, etc.
/// - the two styles cannot be mixed within the same future; use one or the
///   other.
///
/// SemiFuture/Future also provide a back-channel so an interruption request can
///   be sent from consumer to producer; see SemiFuture/Future's `raise()`
///   and Promise's `setInterruptHandler()`.
///
/// The consumer-side SemiFuture/Future objects should generally be accessed
///   via a single thread. That thread is referred to as the 'consumer thread.'
template <class T>
class Future : private futures::detail::FutureBase<T> {
 private:
  using Base = futures::detail::FutureBase<T>;

 public:
  // Export public interface of FutureBase
  // FutureBase is inherited privately to avoid subclasses being cast to
  // a FutureBase pointer
  using typename Base::value_type;

  /// Construct a Future from a value (perfect forwarding)
  template <
      class T2 = T,
      typename = typename std::enable_if<
          !isFuture<typename std::decay<T2>::type>::value &&
          !isSemiFuture<typename std::decay<T2>::type>::value>::type>
  /* implicit */ Future(T2&& val) : Base(std::forward<T2>(val)) {}

  template <class T2 = T>
  /* implicit */ Future(
      typename std::enable_if<std::is_same<Unit, T2>::value>::type* p = nullptr)
      : Base(p) {}

  template <
      class... Args,
      typename std::enable_if<std::is_constructible<T, Args&&...>::value, int>::
          type = 0>
  explicit Future(in_place_t, Args&&... args)
      : Base(in_place, std::forward<Args>(args)...) {}

  Future(Future<T> const&) = delete;
  // movable
  Future(Future<T>&&) noexcept;

  // converting move
  template <
      class T2,
      typename std::enable_if<
          !std::is_same<T, typename std::decay<T2>::type>::value &&
              std::is_constructible<T, T2&&>::value &&
              std::is_convertible<T2&&, T>::value,
          int>::type = 0>
  /* implicit */ Future(Future<T2>&&);
  template <
      class T2,
      typename std::enable_if<
          !std::is_same<T, typename std::decay<T2>::type>::value &&
              std::is_constructible<T, T2&&>::value &&
              !std::is_convertible<T2&&, T>::value,
          int>::type = 0>
  explicit Future(Future<T2>&&);
  template <
      class T2,
      typename std::enable_if<
          !std::is_same<T, typename std::decay<T2>::type>::value &&
              std::is_constructible<T, T2&&>::value,
          int>::type = 0>
  Future& operator=(Future<T2>&&);

  using Base::cancel;
  using Base::hasException;
  using Base::hasValue;
  using Base::isReady;
  using Base::poll;
  using Base::raise;
  using Base::setCallback_;
  using Base::valid;
  using Base::value;
  using Base::result;

  static Future<T> makeEmpty(); // equivalent to moved-from

  // not copyable
  Future& operator=(Future const&) = delete;

  // movable
  Future& operator=(Future&&) noexcept;

  /// Call e->drive() repeatedly until the future is fulfilled. Examples
  /// of DrivableExecutor include EventBase and ManualExecutor. Returns the
  /// value (moved out), or throws the exception.
  T getVia(DrivableExecutor* e);

  /// getVia but will wait only until timed out. Returns the
  /// Try of the value (moved out) or may throw a FutureTimeout exception.
  T getVia(TimedDrivableExecutor* e, Duration dur);

  /// Call e->drive() repeatedly until the future is fulfilled. Examples
  /// of DrivableExecutor include EventBase and ManualExecutor. Returns a
  /// reference to the Try of the value.
  Try<T>& getTryVia(DrivableExecutor* e);

  /// getTryVia but will wait only until timed out. Returns the
  /// Try of the value (moved out) or may throw a FutureTimeout exception.
  Try<T>& getTryVia(TimedDrivableExecutor* e, Duration dur);

  /// Unwraps the case of a Future<Future<T>> instance, and returns a simple
  /// Future<T> instance.
  template <class F = T>
  typename std::
      enable_if<isFuture<F>::value, Future<typename isFuture<T>::Inner>>::type
      unwrap();

  /// Returns a Future which will call back on the other side of executor.
  ///
  /// The ref-qualifier allows for `this` to be moved out so we
  /// don't get access-after-free situations in chaining.
  /// https://akrzemi1.wordpress.com/2014/06/02/ref-qualifiers/
  Future<T> via(Executor* executor, int8_t priority = Executor::MID_PRI) &&;

  Future<T> via(
      Executor::KeepAlive<> executor,
      int8_t priority = Executor::MID_PRI) &&;

  /// This variant creates a new future, where the ref-qualifier && version
  /// moves `this` out. This one is less efficient but avoids confusing users
  /// when "return f.via(x);" fails.
  Future<T> via(Executor* executor, int8_t priority = Executor::MID_PRI) &;

  Future<T> via(
      Executor::KeepAlive<> executor,
      int8_t priority = Executor::MID_PRI) &;

  /** When this Future has completed, execute func which is a function that
    takes one of:
      (const) Try<T>&&
      (const) Try<T>&
      (const) Try<T>
      (const) T&&
      (const) T&
      (const) T
      (void)

    Func shall return either another Future or a value.

    A Future for the return type of func is returned.

    Future<string> f2 = f1.then([](Try<T>&&) { return string("foo"); });

    The Future given to the functor is ready, and the functor may call
    value(), which may rethrow if this has captured an exception. If func
    throws, the exception will be captured in the Future that is returned.
    */
  template <typename F, typename R = futures::detail::callableResult<T, F>>
  typename R::Return then(F&& func) {
    return this->template thenImplementation<F, R>(
        std::forward<F>(func), typename R::Arg());
  }

  /// Variant where func is an member function
  ///
  ///   struct Worker { R doWork(Try<T>); }
  ///
  ///   Worker *w;
  ///   Future<R> f2 = f1.then(&Worker::doWork, w);
  ///
  /// This is just sugar for
  ///
  ///   f1.then(std::bind(&Worker::doWork, w));
  template <typename R, typename Caller, typename... Args>
  Future<typename isFuture<R>::Inner> then(
      R (Caller::*func)(Args...),
      Caller* instance);

  /// Execute the callback via the given Executor. The executor doesn't stick.
  ///
  /// Contrast
  ///
  ///   f.via(x).then(b).then(c)
  ///
  /// with
  ///
  ///   f.then(x, b).then(c)
  ///
  /// In the former both b and c execute via x. In the latter, only b executes
  /// via x, and c executes via the same executor (if any) that f had.
  template <class Executor, class Arg, class... Args>
  auto then(Executor* x, Arg&& arg, Args&&... args) {
    auto oldX = this->getExecutor();
    this->setExecutor(x);
    return this->then(std::forward<Arg>(arg), std::forward<Args>(args)...)
        .via(oldX);
  }

  /** When this Future has completed, execute func which is a function that
    takes one of:
      (const) Try<T>&&
      (const) Try<T>&
      (const) Try<T>

    Func shall return either another Future or a value.

    A Future for the return type of func is returned.

    Future<string> f2 = f1.then([](Try<T>&&) { return string("foo"); });

    The Future given to the functor is ready, and the functor may call
    value(), which may rethrow if this has captured an exception. If func
    throws, the exception will be captured in the Future that is returned.
    */
  template <typename F>
  Future<typename futures::detail::tryCallableResult<T, F>::value_type> thenTry(
      F&& func) &&;

  /** When this Future has completed, execute func which is a function that
    takes one of:
      (const) T&&
      (const) T&
      (const) T
      (void)
    */
  template <typename F>
  Future<typename futures::detail::valueCallableResult<T, F>::value_type>
  thenValue(F&& func) &&;

  template <typename R, typename... Args>
  auto thenValue(R (&func)(Args...)) && {
    return std::move(*this).thenValue(&func);
  }

  /// Set an error callback for this Future. The callback should take a
  /// single argument of the type that you want to catch, and should return
  /// T, SemiFuture<T> or Future<T>
  /// (see overload below). For instance,
  ///
  /// makeFuture()
  ///   .thenTry([] {
  ///     throw std::runtime_error("oh no!");
  ///     return 42;
  ///   })
  ///   .thenError<std::runtime_error>([] (auto const& e) {
  ///     LOG(INFO) << "std::runtime_error: " << e.what();
  ///     return -1; // or makeSemiFuture<int>(-1)
  ///   });
  /// Overload of thenError where continuation can be called with a known
  /// exception type and returns T or Future<T>
  template <class ExceptionType, class F>
  Future<T> thenError(F&& func) &&;

  template <class ExceptionType, class R, class... Args>
  Future<T> thenError(R (&func)(Args...)) && {
    return std::move(*this).template thenError<ExceptionType>(&func);
  }

  /// Overload of thenError where continuation can be called with
  /// exception_wrapper&& and returns T or Future<T>
  template <class F>
  Future<T> thenError(F&& func) &&;

  template <class R, class... Args>
  Future<T> thenError(R (&func)(Args...)) && {
    return std::move(*this).thenError(&func);
  }

  /// Convenience method for ignoring the value and creating a Future<Unit>.
  /// Exceptions still propagate.
  /// This function is identical to .unit().
  Future<Unit> then();

  /// Convenience method for ignoring the value and creating a Future<Unit>.
  /// Exceptions still propagate.
  /// This function is identical to parameterless .then().
  Future<Unit> unit() {
    return then();
  }

  /// Set an error callback for this Future. The callback should take a single
  /// argument of the type that you want to catch, and should return a value of
  /// the same type as this Future, or a Future of that type (see overload
  /// below). For instance,
  ///
  /// makeFuture()
  ///   .thenValue([] {
  ///     throw std::runtime_error("oh no!");
  ///     return 42;
  ///   })
  ///   .onError([] (std::runtime_error& e) {
  ///     LOG(INFO) << "std::runtime_error: " << e.what();
  ///     return -1; // or makeFuture<int>(-1)
  ///   });
  template <class F>
  typename std::enable_if<
      !is_invocable<F, exception_wrapper>::value &&
          !futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError where the error callback returns a Future<T>
  template <class F>
  typename std::enable_if<
      !is_invocable<F, exception_wrapper>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError that takes exception_wrapper and returns Future<T>
  template <class F>
  typename std::enable_if<
      is_invocable<F, exception_wrapper>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError that takes exception_wrapper and returns T
  template <class F>
  typename std::enable_if<
      is_invocable<F, exception_wrapper>::value &&
          !futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// func is like std::function<void()> and is executed unconditionally, and
  /// the value/exception is passed through to the resulting Future.
  /// func shouldn't throw, but if it does it will be captured and propagated,
  /// and discard any value/exception that this Future has obtained.
  template <class F>
  Future<T> ensure(F&& func);

  /// Like onError, but for timeouts. example:
  ///
  ///   Future<int> f = makeFuture<int>(42)
  ///     .delayed(long_time)
  ///     .onTimeout(short_time,
  ///       []() -> int{ return -1; });
  ///
  /// or perhaps
  ///
  ///   Future<int> f = makeFuture<int>(42)
  ///     .delayed(long_time)
  ///     .onTimeout(short_time,
  ///       []() { return makeFuture<int>(some_exception); });
  template <class F>
  Future<T> onTimeout(Duration, F&& func, Timekeeper* = nullptr);

  /// Throw FutureTimeout if this Future does not complete within the given
  /// duration from now. The optional Timeekeeper is as with futures::sleep().
  Future<T> within(Duration, Timekeeper* = nullptr);

  /// Throw the given exception if this Future does not complete within the
  /// given duration from now. The optional Timeekeeper is as with
  /// futures::sleep().
  template <class E>
  Future<T> within(Duration, E exception, Timekeeper* = nullptr);

  /// Delay the completion of this Future for at least this duration from
  /// now. The optional Timekeeper is as with futures::sleep().
  /// NOTE: Deprecated
  /// WARNING: Returned future may complete on Timekeeper thread.
  Future<T> delayedUnsafe(Duration, Timekeeper* = nullptr);

  /// Block until the future is fulfilled. Returns the value (moved out), or
  /// throws the exception. The future must not already have a callback.
  T get();

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// value (moved out), or throws the exception (which might be a FutureTimeout
  /// exception).
  T get(Duration dur);

  /** A reference to the Try of the value */
  Try<T>& getTry();

  /// Block until this Future is complete. Returns a reference to this Future.
  Future<T>& wait() &;

  /// Overload of wait() for rvalue Futures
  Future<T>&& wait() &&;

  /// Block until this Future is complete or until the given Duration passes.
  /// Returns a reference to this Future
  Future<T>& wait(Duration) &;

  /// Overload of wait(Duration) for rvalue Futures
  Future<T>&& wait(Duration) &&;

  /// Call e->drive() repeatedly until the future is fulfilled. Examples
  /// of DrivableExecutor include EventBase and ManualExecutor. Returns a
  /// reference to this Future so that you can chain calls if desired.
  /// value (moved out), or throws the exception.
  Future<T>& waitVia(DrivableExecutor* e) &;

  /// Overload of waitVia() for rvalue Futures
  Future<T>&& waitVia(DrivableExecutor* e) &&;

  /// As waitVia but may return early after dur passes.
  Future<T>& waitVia(TimedDrivableExecutor* e, Duration dur) &;

  /// Overload of waitVia() for rvalue Futures
  /// As waitVia but may return early after dur passes.
  Future<T>&& waitVia(TimedDrivableExecutor* e, Duration dur) &&;

  /// If the value in this Future is equal to the given Future, when they have
  /// both completed, the value of the resulting Future<bool> will be true. It
  /// will be false otherwise (including when one or both Futures have an
  /// exception)
  Future<bool> willEqual(Future<T>&);

  /// predicate behaves like std::function<bool(T const&)>
  /// If the predicate does not obtain with the value, the result
  /// is a folly::FuturePredicateDoesNotObtain exception
  template <class F>
  Future<T> filter(F&& predicate);

  /// Like reduce, but works on a Future<std::vector<T / Try<T>>>, for example
  /// the result of collect or collectAll
  template <class I, class F>
  Future<I> reduce(I&& initial, F&& func);

  /// Create a Future chain from a sequence of callbacks. i.e.
  ///
  ///   f.then(a).then(b).then(c)
  ///
  /// where f is a Future<A> and the result of the chain is a Future<D>
  /// becomes
  ///
  ///   f.thenMulti(a, b, c);
  template <class Callback, class... Callbacks>
  auto thenMulti(Callback&& fn, Callbacks&&... fns) {
    // thenMulti with two callbacks is just then(a).thenMulti(b, ...)
    return then(std::forward<Callback>(fn))
        .thenMulti(std::forward<Callbacks>(fns)...);
  }

  template <class Callback>
  auto thenMulti(Callback&& fn) {
    // thenMulti with one callback is just a then
    return then(std::forward<Callback>(fn));
  }

  /// Create a Future chain from a sequence of callbacks. i.e.
  ///
  ///   f.via(executor).then(a).then(b).then(c).via(oldExecutor)
  ///
  /// where f is a Future<A> and the result of the chain is a Future<D>
  /// becomes
  ///
  ///   f.thenMultiWithExecutor(executor, a, b, c);
  template <class Callback, class... Callbacks>
  auto thenMultiWithExecutor(Executor* x, Callback&& fn, Callbacks&&... fns) {
    // thenMultiExecutor with two callbacks is
    // via(x).then(a).thenMulti(b, ...).via(oldX)
    auto oldX = this->getExecutor();
    this->setExecutor(x);
    return then(std::forward<Callback>(fn))
        .thenMulti(std::forward<Callbacks>(fns)...)
        .via(oldX);
  }

  template <class Callback>
  auto thenMultiWithExecutor(Executor* x, Callback&& fn) {
    // thenMulti with one callback is just a then with an executor
    return then(x, std::forward<Callback>(fn));
  }

  // Convert this Future to a SemiFuture to safely export from a library
  // without exposing a continuation interface
  SemiFuture<T> semi() {
    return SemiFuture<T>{std::move(*this)};
  }

 protected:
  friend class Promise<T>;
  template <class>
  friend class futures::detail::FutureBase;
  template <class>
  friend class Future;
  template <class>
  friend class SemiFuture;
  template <class>
  friend class FutureSplitter;

  using Base::setExecutor;
  using Base::throwIfInvalid;
  using typename Base::corePtr;

  explicit Future(corePtr obj) : Base(obj) {}

  explicit Future(futures::detail::EmptyConstruct) noexcept
      : Base(futures::detail::EmptyConstruct{}) {}

  template <class T2>
  friend Future<T2> makeFuture(Try<T2>&&);

  /// Repeat the given future (i.e., the computation it contains)
  /// n times.
  ///
  /// thunk behaves like std::function<Future<T2>(void)>
  template <class F>
  friend Future<Unit> times(int n, F&& thunk);

  /// Carry out the computation contained in the given future if
  /// the predicate holds.
  ///
  /// thunk behaves like std::function<Future<T2>(void)>
  template <class F>
  friend Future<Unit> when(bool p, F&& thunk);

  /// Carry out the computation contained in the given future if
  /// while the predicate continues to hold.
  ///
  /// thunk behaves like std::function<Future<T2>(void)>
  ///
  /// predicate behaves like std::function<bool(void)>
  template <class P, class F>
  friend Future<Unit> whileDo(P&& predicate, F&& thunk);

  template <class FT>
  friend void futures::detail::convertFuture(
      SemiFuture<FT>&& sf,
      Future<FT>& f);
};

/// A Timekeeper handles the details of keeping time and fulfilling delay
/// promises. The returned Future<Unit> will either complete after the
/// elapsed time, or in the event of some kind of exceptional error may hold
/// an exception. These Futures respond to cancellation. If you use a lot of
/// Delays and many of them ultimately are unneeded (as would be the case for
/// Delays that are used to trigger timeouts of async operations), then you
/// can and should cancel them to reclaim resources.
///
/// Users will typically get one of these via Future::sleep(Duration) or
/// use them implicitly behind the scenes by passing a timeout to some Future
/// operation.
///
/// Although we don't formally alias Delay = Future<Unit>,
/// that's an appropriate term for it. People will probably also call these
/// Timeouts, and that's ok I guess, but that term is so overloaded I thought
/// it made sense to introduce a cleaner term.
///
/// Remember that Duration is a std::chrono duration (millisecond resolution
/// at the time of writing). When writing code that uses specific durations,
/// prefer using the explicit std::chrono type, e.g. std::chrono::milliseconds
/// over Duration. This makes the code more legible and means you won't be
/// unpleasantly surprised if we redefine Duration to microseconds, or
/// something.
///
///    timekeeper.after(std::chrono::duration_cast<Duration>(
///      someNanoseconds))
class Timekeeper {
 public:
  virtual ~Timekeeper() = default;

  /// Returns a future that will complete after the given duration with the
  /// elapsed time. Exceptional errors can happen but they must be
  /// exceptional. Use the steady (monotonic) clock.
  ///
  /// You may cancel this Future to reclaim resources.
  ///
  /// This future probably completes on the timer thread. You should almost
  /// certainly follow it with a via() call or the accuracy of other timers
  /// will suffer.
  virtual Future<Unit> after(Duration) = 0;

  /// Returns a future that will complete at the requested time.
  ///
  /// You may cancel this Future to reclaim resources.
  ///
  /// NB This is sugar for `after(when - now)`, so while you are welcome to
  /// use a std::chrono::system_clock::time_point it will not track changes to
  /// the system clock but rather execute that many milliseconds in the future
  /// according to the steady clock.
  template <class Clock>
  Future<Unit> at(std::chrono::time_point<Clock> when);
};

template <class T>
std::pair<Promise<T>, Future<T>> makePromiseContract(Executor* e) {
  auto p = Promise<T>();
  auto f = p.getSemiFuture().via(e);
  return std::make_pair(std::move(p), std::move(f));
}

} // namespace folly

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace detail {
template <typename T>
class FutureAwaitable {
 public:
  explicit FutureAwaitable(folly::Future<T>&& future)
      : future_(std::move(future)) {}

  bool await_ready() const {
    return future_.isReady();
  }

  T await_resume() {
    return std::move(future_.value());
  }

  void await_suspend(std::experimental::coroutine_handle<> h) {
    future_.setCallback_([h](Try<T>&&) mutable { h(); });
  }

 private:
  folly::Future<T> future_;
};

template <typename T>
class FutureRefAwaitable {
 public:
  explicit FutureRefAwaitable(folly::Future<T>& future) : future_(future) {}

  bool await_ready() const {
    return future_.isReady();
  }

  T await_resume() {
    return std::move(future_.value());
  }

  void await_suspend(std::experimental::coroutine_handle<> h) {
    future_.setCallback_([h](Try<T>&&) mutable { h(); });
  }

 private:
  folly::Future<T>& future_;
};
} // namespace detail
} // namespace folly

template <typename T>
folly::detail::FutureAwaitable<T>
/* implicit */ operator co_await(folly::Future<T>& future) {
  return folly::detail::FutureRefAwaitable<T>(future);
}
#endif

#include <folly/futures/Future-inl.h>
