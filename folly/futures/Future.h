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
#include <folly/Utility.h>
#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/TimedDrivableExecutor.h>
#include <folly/futures/FutureException.h>
#include <folly/futures/Promise.h>
#include <folly/futures/detail/Types.h>

// boring predeclarations and details
#include <folly/futures/Future-pre.h>

// not-boring helpers, e.g. all in folly::futures, makeFuture variants, etc.
// Needs to be included after Future-pre.h and before Future-inl.h
#include <folly/futures/helpers.h>

namespace folly {

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

  /// Returns a reference to the result, with a reference category and const-
  /// qualification equivalent to the reference category and const-qualification
  /// of the receiver.
  ///
  /// If moved-from, throws NoState.
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
  bool hasValue();

  /// sugar for getTry().hasException()
  bool hasException();

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

  bool isActive() {
    return core_->isActive();
  }

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

  using corePtr = futures::detail::Core<T>*;

  // shared core state object
  corePtr core_;

  explicit FutureBase(corePtr obj) : core_(obj) {}

  explicit FutureBase(futures::detail::EmptyConstruct) noexcept;

  void detach();

  void throwIfInvalid() const;

  template <class FutureType>
  void assign(FutureType&) noexcept;

  Executor* getExecutor() {
    return core_->getExecutor();
  }

  void setExecutor(Executor* x, int8_t priority = Executor::MID_PRI) {
    core_->setExecutor(x, priority);
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
  using Base::isActive;
  using Base::isReady;
  using Base::poll;
  using Base::raise;
  using Base::result;
  using Base::setCallback_;
  using Base::value;

  SemiFuture& operator=(SemiFuture const&) = delete;
  SemiFuture& operator=(SemiFuture&&) noexcept;
  SemiFuture& operator=(Future<T>&&) noexcept;

  /// Block until the future is fulfilled. Returns the value (moved out), or
  /// throws the exception. The future must not already have a callback.
  T get() &&;

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// value (moved out), or throws the exception (which might be a TimedOut
  /// exception).
  T get(Duration dur) &&;

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// Try of the value (moved out).
  Try<T> getTry() &&;

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// Try of the value (moved out) or may throw a TimedOut exception.
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

  /// Returns an inactive Future which will call back on the other side of
  /// executor (when it is activated).
  ///
  /// NB remember that Futures activate when they destruct. This is good,
  /// it means that this will work:
  ///
  ///   f.via(e).then(a).then(b);
  ///
  /// a and b will execute in the same context (the far side of e), because
  /// the Future (temporary variable) created by via(e) does not call back
  /// until it destructs, which is after then(a) and then(b) have been wired
  /// up.
  ///
  /// But this is still racy:
  ///
  ///   f = f.via(e).then(a);
  ///   f.then(b);
  // The ref-qualifier allows for `this` to be moved out so we
  // don't get access-after-free situations in chaining.
  // https://akrzemi1.wordpress.com/2014/06/02/ref-qualifiers/
  inline Future<T> via(
      Executor* executor,
      int8_t priority = Executor::MID_PRI) &&;

  /**
   * Defer work to run on the consumer of the future.
   * Function must take a Try as a parameter.
   * This work will be run eithe ron an executor that the caller sets on the
   * SemiFuture, or inline with the call to .get().
   * NB: This is a custom method because boost-blocking executors is a
   * special-case for work deferral in folly. With more general boost-blocking
   * support all executors would boost block and we would simply use some form
   * of driveable executor here.
   */
  template <typename F>
  SemiFuture<
      typename futures::detail::deferCallableResult<T, F>::Return::value_type>
  defer(F&& func) &&;

  /**
   * Defer for functions taking a T rather than a Try<T>.
   */
  template <typename F>
  SemiFuture<typename futures::detail::deferValueCallableResult<T, F>::Return::
                 value_type>
  deferValue(F&& func) &&;

  /// Set an error callback for this SemiFuture. The callback should take a
  /// single argument of the type that you want to catch, and should return a
  /// value of the same type as this SemiFuture, or a SemiFuture of that type
  /// (see overload below). For instance,
  ///
  /// makeSemiFuture()
  ///   .defer([] {
  ///     throw std::runtime_error("oh no!");
  ///     return 42;
  ///   })
  ///   .deferError([] (std::runtime_error& e) {
  ///     LOG(INFO) << "std::runtime_error: " << e.what();
  ///     return -1; // or makeSemiFuture<int>(-1)
  ///   });
  template <class F>
  typename std::enable_if<
      !futures::detail::callableWith<F, exception_wrapper>::value &&
          !futures::detail::callableWith<F, exception_wrapper&>::value &&
          !futures::detail::Extract<F>::ReturnsFuture::value,
      SemiFuture<T>>::type
  deferError(F&& func);

  /// Overload of deferError where the error callback returns a Future<T>
  template <class F>
  typename std::enable_if<
      !futures::detail::callableWith<F, exception_wrapper>::value &&
          !futures::detail::callableWith<F, exception_wrapper&>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      SemiFuture<T>>::type
  deferError(F&& func);

  /// Overload of deferError that takes exception_wrapper and returns T
  template <class F>
  typename std::enable_if<
      futures::detail::callableWith<F, exception_wrapper>::value &&
          !futures::detail::Extract<F>::ReturnsFuture::value,
      SemiFuture<T>>::type
  deferError(F&& func);

  /// Overload of deferError that takes exception_wrapper and returns Future<T>
  template <class F>
  typename std::enable_if<
      futures::detail::callableWith<F, exception_wrapper>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      SemiFuture<T>>::type
  deferError(F&& func);

  /// Return a future that completes inline, as if the future had no executor.
  /// Intended for porting legacy code without behavioural change, and for rare
  /// cases where this is really the intended behaviour.
  /// Future is unsafe in the sense that the executor it completes on is
  /// non-deterministic in the standard case.
  /// For new code, or to update code that temporarily uses this, please
  /// use via and pass a meaningful executor.
  inline Future<T> toUnsafeFuture() &&;

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
  using Base::isActive;
  using Base::isReady;
  using Base::poll;
  using Base::raise;
  using Base::setCallback_;
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
  /// Try of the value (moved out) or may throw a TimedOut exception.
  T getVia(TimedDrivableExecutor* e, Duration dur);

  /// Call e->drive() repeatedly until the future is fulfilled. Examples
  /// of DrivableExecutor include EventBase and ManualExecutor. Returns a
  /// reference to the Try of the value.
  Try<T>& getTryVia(DrivableExecutor* e);

  /// getTryVia but will wait only until timed out. Returns the
  /// Try of the value (moved out) or may throw a TimedOut exception.
  Try<T>& getTryVia(TimedDrivableExecutor* e, Duration dur);

  /// Unwraps the case of a Future<Future<T>> instance, and returns a simple
  /// Future<T> instance.
  template <class F = T>
  typename std::
      enable_if<isFuture<F>::value, Future<typename isFuture<T>::Inner>>::type
      unwrap();

  /// Returns an inactive Future which will call back on the other side of
  /// executor (when it is activated).
  ///
  /// NB remember that Futures activate when they destruct. This is good,
  /// it means that this will work:
  ///
  ///   f.via(e).then(a).then(b);
  ///
  /// a and b will execute in the same context (the far side of e), because
  /// the Future (temporary variable) created by via(e) does not call back
  /// until it destructs, which is after then(a) and then(b) have been wired
  /// up.
  ///
  /// But this is still racy:
  ///
  ///   f = f.via(e).then(a);
  ///   f.then(b);
  // The ref-qualifier allows for `this` to be moved out so we
  // don't get access-after-free situations in chaining.
  // https://akrzemi1.wordpress.com/2014/06/02/ref-qualifiers/
  inline Future<T> via(
      Executor* executor,
      int8_t priority = Executor::MID_PRI) &&;

  /// This variant creates a new future, where the ref-qualifier && version
  /// moves `this` out. This one is less efficient but avoids confusing users
  /// when "return f.via(x);" fails.
  inline Future<T> via(
      Executor* executor,
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
  ///   .then([] {
  ///     throw std::runtime_error("oh no!");
  ///     return 42;
  ///   })
  ///   .onError([] (std::runtime_error& e) {
  ///     LOG(INFO) << "std::runtime_error: " << e.what();
  ///     return -1; // or makeFuture<int>(-1)
  ///   });
  template <class F>
  typename std::enable_if<
      !futures::detail::callableWith<F, exception_wrapper>::value &&
          !futures::detail::callableWith<F, exception_wrapper&>::value &&
          !futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError where the error callback returns a Future<T>
  template <class F>
  typename std::enable_if<
      !futures::detail::callableWith<F, exception_wrapper>::value &&
          !futures::detail::callableWith<F, exception_wrapper&>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError that takes exception_wrapper and returns Future<T>
  template <class F>
  typename std::enable_if<
      futures::detail::callableWith<F, exception_wrapper>::value &&
          futures::detail::Extract<F>::ReturnsFuture::value,
      Future<T>>::type
  onError(F&& func);

  /// Overload of onError that takes exception_wrapper and returns T
  template <class F>
  typename std::enable_if<
      futures::detail::callableWith<F, exception_wrapper>::value &&
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

  /// A Future's callback is executed when all three of these conditions have
  /// become true: it has a value (set by the Promise), it has a callback (set
  /// by then), and it is active (active by default).
  ///
  /// Inactive Futures will activate upon destruction.
  [[deprecated("do not use")]] Future<T>& activate() & {
    this->core_->activate();
    return *this;
  }
  [[deprecated("do not use")]] Future<T>& deactivate() & {
    this->core_->deactivate();
    return *this;
  }
  [[deprecated("do not use")]] Future<T> activate() && {
    this->core_->activate();
    return std::move(*this);
  }
  [[deprecated("do not use")]] Future<T> deactivate() && {
    this->core_->deactivate();
    return std::move(*this);
  }

  /// Throw TimedOut if this Future does not complete within the given
  /// duration from now. The optional Timeekeeper is as with futures::sleep().
  Future<T> within(Duration, Timekeeper* = nullptr);

  /// Throw the given exception if this Future does not complete within the
  /// given duration from now. The optional Timeekeeper is as with
  /// futures::sleep().
  template <class E>
  Future<T> within(Duration, E exception, Timekeeper* = nullptr);

  /// Delay the completion of this Future for at least this duration from
  /// now. The optional Timekeeper is as with futures::sleep().
  Future<T> delayed(Duration, Timekeeper* = nullptr);

  /// Block until the future is fulfilled. Returns the value (moved out), or
  /// throws the exception. The future must not already have a callback.
  T get();

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// value (moved out), or throws the exception (which might be a TimedOut
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
  /// is a folly::PredicateDoesNotObtain exception
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
