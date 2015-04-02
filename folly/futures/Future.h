/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/Optional.h>
#include <folly/MoveWrapper.h>
#include <folly/futures/Deprecated.h>
#include <folly/futures/DrivableExecutor.h>
#include <folly/futures/Promise.h>
#include <folly/futures/Try.h>
#include <folly/futures/FutureException.h>
#include <folly/futures/detail/Types.h>

namespace folly {

template <class> struct Promise;

template <typename T>
struct isFuture : std::false_type {
  typedef T Inner;
};

template <typename T>
struct isFuture<Future<T>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isTry : std::false_type {};

template <typename T>
struct isTry<Try<T>> : std::true_type {};

namespace detail {

template <class> struct Core;
template <class...> struct VariadicContext;

template<typename F, typename... Args>
using resultOf = decltype(std::declval<F>()(std::declval<Args>()...));

template <typename...>
struct ArgType;

template <typename Arg, typename... Args>
struct ArgType<Arg, Args...> {
  typedef Arg FirstArg;
};

template <>
struct ArgType<> {
  typedef void FirstArg;
};

template <bool isTry, typename F, typename... Args>
struct argResult {
  typedef resultOf<F, Args...> Result;
};

template<typename F, typename... Args>
struct callableWith {
    template<typename T,
             typename = detail::resultOf<T, Args...>>
    static constexpr std::true_type
    check(std::nullptr_t) { return std::true_type{}; };

    template<typename>
    static constexpr std::false_type
    check(...) { return std::false_type{}; };

    typedef decltype(check<F>(nullptr)) type;
    static constexpr bool value = type::value;
};

template<typename T, typename F>
struct callableResult {
  typedef typename std::conditional<
    callableWith<F>::value,
    detail::argResult<false, F>,
    typename std::conditional<
      callableWith<F, T&&>::value,
      detail::argResult<false, F, T&&>,
      typename std::conditional<
        callableWith<F, T&>::value,
        detail::argResult<false, F, T&>,
        typename std::conditional<
          callableWith<F, Try<T>&&>::value,
          detail::argResult<true, F, Try<T>&&>,
          detail::argResult<true, F, Try<T>&>>::type>::type>::type>::type Arg;
  typedef isFuture<typename Arg::Result> ReturnsFuture;
  typedef Future<typename ReturnsFuture::Inner> Return;
};

template<typename F>
struct callableResult<void, F> {
  typedef typename std::conditional<
    callableWith<F>::value,
    detail::argResult<false, F>,
    typename std::conditional<
      callableWith<F, Try<void>&&>::value,
      detail::argResult<true, F, Try<void>&&>,
      detail::argResult<true, F, Try<void>&>>::type>::type Arg;
  typedef isFuture<typename Arg::Result> ReturnsFuture;
  typedef Future<typename ReturnsFuture::Inner> Return;
};

template <typename L>
struct Extract : Extract<decltype(&L::operator())> { };

template <typename Class, typename R, typename... Args>
struct Extract<R(Class::*)(Args...) const> {
  typedef isFuture<R> ReturnsFuture;
  typedef Future<typename ReturnsFuture::Inner> Return;
  typedef typename ReturnsFuture::Inner RawReturn;
  typedef typename ArgType<Args...>::FirstArg FirstArg;
};

template <typename Class, typename R, typename... Args>
struct Extract<R(Class::*)(Args...)> {
  typedef isFuture<R> ReturnsFuture;
  typedef Future<typename ReturnsFuture::Inner> Return;
  typedef typename ReturnsFuture::Inner RawReturn;
  typedef typename ArgType<Args...>::FirstArg FirstArg;
};

} // detail

struct Timekeeper;

/// This namespace is for utility functions that would usually be static
/// members of Future, except they don't make sense there because they don't
/// depend on the template type (rather, on the type of their arguments in
/// some cases). This is the least-bad naming scheme we could think of. Some
/// of the functions herein have really-likely-to-collide names, like "map"
/// and "sleep".
namespace futures {
  /// Returns a Future that will complete after the specified duration. The
  /// Duration typedef of a `std::chrono` duration type indicates the
  /// resolution you can expect to be meaningful (milliseconds at the time of
  /// writing). Normally you wouldn't need to specify a Timekeeper, we will
  /// use the global futures timekeeper (we run a thread whose job it is to
  /// keep time for futures timeouts) but we provide the option for power
  /// users.
  ///
  /// The Timekeeper thread will be lazily created the first time it is
  /// needed. If your program never uses any timeouts or other time-based
  /// Futures you will pay no Timekeeper thread overhead.
  Future<void> sleep(Duration, Timekeeper* = nullptr);

  /// Create a Future chain from a sequence of callbacks. i.e.
  ///
  ///   f.then(a).then(b).then(c);
  ///
  /// where f is a Future<A> and the result of the chain is a Future<Z>
  /// becomes
  ///
  ///   f.then(chain<A,Z>(a, b, c));
  // If anyone figures how to get chain to deduce A and Z, I'll buy you a drink.
  template <class A, class Z, class... Callbacks>
  std::function<Future<Z>(Try<A>)>
  chain(Callbacks... fns);
}

template <class T>
class Future {
 public:
  typedef T value_type;

  // not copyable
  Future(Future const&) = delete;
  Future& operator=(Future const&) = delete;

  // movable
  Future(Future&&) noexcept;
  Future& operator=(Future&&) noexcept;

  // makeFuture
  template <class F = T>
  /* implicit */
  Future(const typename std::enable_if<!std::is_void<F>::value, F>::type& val);

  template <class F = T>
  /* implicit */
  Future(typename std::enable_if<!std::is_void<F>::value, F>::type&& val);

  template <class F = T,
            typename std::enable_if<std::is_void<F>::value, int>::type = 0>
  Future();

  ~Future();

  /** Return the reference to result. Should not be called if !isReady().
    Will rethrow the exception if an exception has been
    captured.
    */
  typename std::add_lvalue_reference<T>::type
  value();
  typename std::add_lvalue_reference<const T>::type
  value() const;

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
  template <typename Executor>
  Future<T> via(Executor* executor) &&;

  /// This variant creates a new future, where the ref-qualifier && version
  /// moves `this` out. This one is less efficient but avoids confusing users
  /// when "return f.via(x);" fails.
  template <typename Executor>
  Future<T> via(Executor* executor) &;

  /** True when the result (or exception) is ready. */
  bool isReady() const;

  /** A reference to the Try of the value */
  Try<T>& getTry();

  /// If the promise has been fulfilled, return an Optional with the Try<T>.
  /// Otherwise return an empty Optional.
  /// Note that this moves the Try<T> out.
  Optional<Try<T>> poll();

  /// Block until the future is fulfilled. Returns the value (moved out), or
  /// throws the exception. The future must not already have a callback.
  T get();

  /// Block until the future is fulfilled, or until timed out. Returns the
  /// value (moved out), or throws the exception (which might be a TimedOut
  /// exception).
  T get(Duration dur);

  /// Call e->drive() repeatedly until the future is fulfilled. Examples
  /// of DrivableExecutor include EventBase and ManualExecutor. Returns the
  /// value (moved out), or throws the exception.
  T getVia(DrivableExecutor* e);

  /// Unwraps the case of a Future<Future<T>> instance, and returns a simple
  /// Future<T> instance.
  template <class F = T>
  typename std::enable_if<isFuture<F>::value,
                          Future<typename isFuture<T>::Inner>>::type
  unwrap();

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
  /* TODO n3428 and other async frameworks have something like then(scheduler,
     Future), we might want to support a similar API which could be
     implemented a little more efficiently than
     f.via(executor).then(callback) */
  template <typename F, typename R = detail::callableResult<T, F>>
  typename R::Return then(F func) {
    typedef typename R::Arg Arguments;
    return thenImplementation<F, R>(std::move(func), Arguments());
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
  Future<typename isFuture<R>::Inner>
  then(R(Caller::*func)(Args...), Caller *instance);

  /// Convenience method for ignoring the value and creating a Future<void>.
  /// Exceptions still propagate.
  Future<void> then();

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
    !detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
  onError(F&& func);

  /// Overload of onError where the error callback returns a Future<T>
  template <class F>
  typename std::enable_if<
    detail::Extract<F>::ReturnsFuture::value,
    Future<T>>::type
  onError(F&& func);

  /// func is like std::function<void()> and is executed unconditionally, and
  /// the value/exception is passed through to the resulting Future.
  /// func shouldn't throw, but if it does it will be captured and propagated,
  /// and discard any value/exception that this Future has obtained.
  template <class F>
  Future<T> ensure(F func);

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

  /// This is not the method you're looking for.
  ///
  /// This needs to be public because it's used by make* and when*, and it's
  /// not worth listing all those and their fancy template signatures as
  /// friends. But it's not for public consumption.
  template <class F>
  void setCallback_(F&& func);

  /// A Future's callback is executed when all three of these conditions have
  /// become true: it has a value (set by the Promise), it has a callback (set
  /// by then), and it is active (active by default).
  ///
  /// Inactive Futures will activate upon destruction.
  Future<T>& activate() & {
    core_->activate();
    return *this;
  }
  Future<T>& deactivate() & {
    core_->deactivate();
    return *this;
  }
  Future<T> activate() && {
    core_->activate();
    return std::move(*this);
  }
  Future<T> deactivate() && {
    core_->deactivate();
    return std::move(*this);
  }

  bool isActive() {
    return core_->isActive();
  }

  template <class E>
  void raise(E&& exception) {
    raise(make_exception_wrapper<typename std::remove_reference<E>::type>(
        std::move(exception)));
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

  /// If the value in this Future is equal to the given Future, when they have
  /// both completed, the value of the resulting Future<bool> will be true. It
  /// will be false otherwise (including when one or both Futures have an
  /// exception)
  Future<bool> willEqual(Future<T>&);

  /// predicate behaves like std::function<bool(T const&)>
  /// If the predicate does not obtain with the value, the result
  /// is a folly::PredicateDoesNotObtain exception
  template <class F>
  Future<T> filter(F predicate);

 protected:
  typedef detail::Core<T>* corePtr;

  // shared core state object
  corePtr core_;

  explicit
  Future(corePtr obj) : core_(obj) {}

  void detach();

  void throwIfInvalid() const;

  friend class Promise<T>;
  template <class> friend class Future;

  // Variant: returns a value
  // e.g. f.then([](Try<T> t){ return t.value(); });
  template <typename F, typename R, bool isTry, typename... Args>
  typename std::enable_if<!R::ReturnsFuture::value, typename R::Return>::type
  thenImplementation(F func, detail::argResult<isTry, F, Args...>);

  // Variant: returns a Future
  // e.g. f.then([](Try<T> t){ return makeFuture<T>(t); });
  template <typename F, typename R, bool isTry, typename... Args>
  typename std::enable_if<R::ReturnsFuture::value, typename R::Return>::type
  thenImplementation(F func, detail::argResult<isTry, F, Args...>);

  Executor* getExecutor() { return core_->getExecutor(); }
  void setExecutor(Executor* x) { core_->setExecutor(x); }
};

/**
  Make a completed Future by moving in a value. e.g.

    string foo = "foo";
    auto f = makeFuture(std::move(foo));

  or

    auto f = makeFuture<string>("foo");
*/
template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t);

/** Make a completed void Future. */
Future<void> makeFuture();

/** Make a completed Future by executing a function. If the function throws
  we capture the exception, otherwise we capture the result. */
template <class F>
auto makeFutureTry(
  F&& func,
  typename std::enable_if<
    !std::is_reference<F>::value, bool>::type sdf = false)
  -> Future<decltype(func())>;

template <class F>
auto makeFutureTry(
  F const& func)
  -> Future<decltype(func())>;

/// Make a failed Future from an exception_ptr.
/// Because the Future's type cannot be inferred you have to specify it, e.g.
///
///   auto f = makeFuture<string>(std::current_exception());
template <class T>
Future<T> makeFuture(std::exception_ptr const& e) DEPRECATED;

/// Make a failed Future from an exception_wrapper.
template <class T>
Future<T> makeFuture(exception_wrapper ew);

/** Make a Future from an exception type E that can be passed to
  std::make_exception_ptr(). */
template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value,
                        Future<T>>::type
makeFuture(E const& e);

/** Make a Future out of a Try */
template <class T>
Future<T> makeFuture(Try<T>&& t);

/*
 * Return a new Future that will call back on the given Executor.
 * This is just syntactic sugar for makeFuture().via(executor)
 *
 * @param executor the Executor to call back on
 *
 * @returns a void Future that will call back on the given executor
 */
template <typename Executor>
Future<void> via(Executor* executor);

/** When all the input Futures complete, the returned Future will complete.
  Errors do not cause early termination; this Future will always succeed
  after all its Futures have finished (whether successfully or with an
  error).

  The Futures are moved in, so your copies are invalid. If you need to
  chain further from these Futures, use the variant with an output iterator.

  This function is thread-safe for Futures running on different threads. But
  if you are doing anything non-trivial after, you will probably want to
  follow with `via(executor)` because it will complete in whichever thread the
  last Future completes in.

  The return type for Future<T> input is a Future<std::vector<Try<T>>>
  */
template <class InputIterator>
Future<std::vector<Try<
  typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAll(InputIterator first, InputIterator last);

/// This version takes a varying number of Futures instead of an iterator.
/// The return type for (Future<T1>, Future<T2>, ...) input
/// is a Future<std::tuple<Try<T1>, Try<T2>, ...>>.
/// The Futures are moved in, so your copies are invalid.
template <typename... Fs>
typename detail::VariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
whenAll(Fs&&... fs);

/** The result is a pair of the index of the first Future to complete and
  the Try. If multiple Futures complete at the same time (or are already
  complete when passed in), the "winner" is chosen non-deterministically.

  This function is thread-safe for Futures running on different threads.
  */
template <class InputIterator>
Future<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAny(InputIterator first, InputIterator last);

/** when n Futures have completed, the Future completes with a vector of
  the index and Try of those n Futures (the indices refer to the original
  order, but the result vector will be in an arbitrary order)

  Not thread safe.
  */
template <class InputIterator>
Future<std::vector<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>>
whenN(InputIterator first, InputIterator last, size_t n);

template <typename F, typename T, typename ItT>
using MaybeTryArg = typename std::conditional<
  detail::callableWith<F, T&&, Try<ItT>&&>::value, Try<ItT>, ItT>::type;

template<typename F, typename T, typename Arg>
using isFutureResult = isFuture<typename std::result_of<F(T&&, Arg&&)>::type>;

/** repeatedly calls func on every result, e.g.
    reduce(reduce(reduce(T initial, result of first), result of second), ...)

    The type of the final result is a Future of the type of the initial value.

    Func can either return a T, or a Future<T>
  */
template <class It, class T, class F,
          class ItT = typename std::iterator_traits<It>::value_type::value_type,
          class Arg = MaybeTryArg<F, T, ItT>>
typename std::enable_if<!isFutureResult<F, T, Arg>::value, Future<T>>::type
reduce(It first, It last, T initial, F func);

template <class It, class T, class F,
          class ItT = typename std::iterator_traits<It>::value_type::value_type,
          class Arg = MaybeTryArg<F, T, ItT>>
typename std::enable_if<isFutureResult<F, T, Arg>::value, Future<T>>::type
reduce(It first, It last, T initial, F func);

} // folly

#include <folly/futures/Future-inl.h>
