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

#include <folly/futures/Future.h>

namespace folly {

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

  /**
   * Set func as the callback for each input Future and return a vector of
   * Futures containing the results in the input order.
   */
  template <class It, class F,
            class ItT = typename std::iterator_traits<It>::value_type,
            class Result
      = typename decltype(std::declval<ItT>().then(std::declval<F>()))::value_type>
  std::vector<Future<Result>> map(It first, It last, F func);

  // Sugar for the most common case
  template <class Collection, class F>
  auto map(Collection&& c, F&& func)
      -> decltype(map(c.begin(), c.end(), func)) {
    return map(c.begin(), c.end(), std::forward<F>(func));
  }

}

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
auto makeFutureWith(
  F&& func,
  typename std::enable_if<
    !std::is_reference<F>::value, bool>::type sdf = false)
  -> Future<decltype(func())>;

template <class F>
auto makeFutureWith(
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
inline Future<void> via(Executor* executor);

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
collectAll(InputIterator first, InputIterator last);

/// Sugar for the most common case
template <class Collection>
auto collectAll(Collection&& c) -> decltype(collectAll(c.begin(), c.end())) {
  return collectAll(c.begin(), c.end());
}

/// This version takes a varying number of Futures instead of an iterator.
/// The return type for (Future<T1>, Future<T2>, ...) input
/// is a Future<std::tuple<Try<T1>, Try<T2>, ...>>.
/// The Futures are moved in, so your copies are invalid.
template <typename... Fs>
typename detail::VariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
collectAll(Fs&&... fs);

/// Like collectAll, but will short circuit on the first exception. Thus, the
/// type of the returned Future is std::vector<T> instead of
/// std::vector<Try<T>>
template <class InputIterator>
Future<typename detail::CollectContext<
  typename std::iterator_traits<InputIterator>::value_type::value_type
>::result_type>
collect(InputIterator first, InputIterator last);

/// Sugar for the most common case
template <class Collection>
auto collect(Collection&& c) -> decltype(collect(c.begin(), c.end())) {
  return collect(c.begin(), c.end());
}

/** The result is a pair of the index of the first Future to complete and
  the Try. If multiple Futures complete at the same time (or are already
  complete when passed in), the "winner" is chosen non-deterministically.

  This function is thread-safe for Futures running on different threads.
  */
template <class InputIterator>
Future<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAny(InputIterator first, InputIterator last);

/// Sugar for the most common case
template <class Collection>
auto collectAny(Collection&& c) -> decltype(collectAny(c.begin(), c.end())) {
  return collectAny(c.begin(), c.end());
}

/** when n Futures have completed, the Future completes with a vector of
  the index and Try of those n Futures (the indices refer to the original
  order, but the result vector will be in an arbitrary order)

  Not thread safe.
  */
template <class InputIterator>
Future<std::vector<std::pair<
  size_t,
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>>
collectN(InputIterator first, InputIterator last, size_t n);

/// Sugar for the most common case
template <class Collection>
auto collectN(Collection&& c, size_t n)
    -> decltype(collectN(c.begin(), c.end(), n)) {
  return collectN(c.begin(), c.end(), n);
}

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
template <class It, class T, class F>
Future<T> reduce(It first, It last, T&& initial, F&& func);

/// Sugar for the most common case
template <class Collection, class T, class F>
auto reduce(Collection&& c, T&& initial, F&& func)
    -> decltype(reduce(c.begin(), c.end(), std::forward<T>(initial),
                std::forward<F>(func))) {
  return reduce(
      c.begin(),
      c.end(),
      std::forward<T>(initial),
      std::forward<F>(func));
}

} // namespace folly
