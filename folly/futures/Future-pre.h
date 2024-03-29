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

// included by Future.h, do not include directly.

namespace folly {

template <class>
class Promise;

template <class T>
class SemiFuture;

template <typename T>
struct isSemiFuture : std::false_type {
  using Inner = lift_unit_t<T>;
};

template <typename T>
struct isSemiFuture<SemiFuture<T>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isFuture : std::false_type {
  using Inner = lift_unit_t<T>;
};

template <typename T>
struct isFuture<Future<T>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isFutureOrSemiFuture : std::false_type {
  using Inner = lift_unit_t<T>;
};

template <typename T>
struct isFutureOrSemiFuture<Try<T>> : std::false_type {
  using Inner = lift_unit_t<T>;
};

template <typename T>
struct isFutureOrSemiFuture<Future<T>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isFutureOrSemiFuture<Future<Try<T>>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isFutureOrSemiFuture<SemiFuture<T>> : std::true_type {
  typedef T Inner;
};

template <typename T>
struct isFutureOrSemiFuture<SemiFuture<Try<T>>> : std::true_type {
  typedef T Inner;
};

namespace futures {
namespace detail {

template <typename...>
struct ArgType;

template <typename Arg, typename... Args>
struct ArgType<Arg, Args...> {
  typedef Arg FirstArg;
  typedef ArgType<Args...> Tail;
};

template <>
struct ArgType<> {
  typedef void FirstArg;
};

template <bool isTry_, typename F, typename... Args>
struct argResult {
  using ArgList = ArgType<Args...>;
  using Result = invoke_result_t<F, Args...>;
  using ArgsSize = index_constant<sizeof...(Args)>;
  static constexpr bool isTry() { return isTry_; }
};

template <typename T, typename F>
struct tryCallableResult {
  typedef detail::argResult<true, F, Try<T>&&> Arg;
  typedef isFutureOrSemiFuture<typename Arg::Result> ReturnsFuture;
  typedef typename ReturnsFuture::Inner value_type;
};

template <typename T, typename F>
struct tryExecutorCallableResult {
  typedef detail::argResult<true, F, Executor::KeepAlive<>&&, Try<T>&&> Arg;
  typedef isFutureOrSemiFuture<typename Arg::Result> ReturnsFuture;
  typedef typename ReturnsFuture::Inner value_type;
};

template <typename T, typename F>
struct valueCallableResult {
  typedef detail::argResult<false, F, T&&> Arg;
  typedef isFutureOrSemiFuture<typename Arg::Result> ReturnsFuture;
  typedef typename ReturnsFuture::Inner value_type;
};

template <typename T, typename F>
struct valueExecutorCallableResult {
  typedef detail::argResult<false, F, Executor::KeepAlive<>&&, T&&> Arg;
  typedef isFutureOrSemiFuture<typename Arg::Result> ReturnsFuture;
  typedef typename ReturnsFuture::Inner value_type;
};

class DeferredExecutor;

} // namespace detail
} // namespace futures

class Timekeeper;

} // namespace folly
