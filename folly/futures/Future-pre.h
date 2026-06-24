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
  using Inner = T;
};

template <typename T>
struct isFuture : std::false_type {
  using Inner = lift_unit_t<T>;
};

template <typename T>
struct isFuture<Future<T>> : std::true_type {
  using Inner = T;
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
  using Inner = T;
};

template <typename T>
struct isFutureOrSemiFuture<Future<Try<T>>> : std::true_type {
  using Inner = T;
};

template <typename T>
struct isFutureOrSemiFuture<SemiFuture<T>> : std::true_type {
  using Inner = T;
};

template <typename T>
struct isFutureOrSemiFuture<SemiFuture<Try<T>>> : std::true_type {
  using Inner = T;
};

// C++20 concepts for Future/SemiFuture type constraints.
// These wrap the isFuture/isSemiFuture/isFutureOrSemiFuture traits and are
// used throughout Future.h and Future-inl.h to replace enable_if SFINAE.
template <typename T>
concept FutureType = isFuture<T>::value;

template <typename T>
concept SemiFutureType = isSemiFuture<T>::value;

template <typename T>
concept FutureOrSemiFutureType = isFutureOrSemiFuture<T>::value;

namespace futures {
namespace detail {
template <class T>
class FutureBase;
} // namespace detail
} // namespace futures

template <typename T>
struct isFutureBase : std::false_type {};

template <typename T>
struct isFutureBase<futures::detail::FutureBase<T>> : std::true_type {};

template <typename T>
concept FutureBaseType = isFutureBase<T>::value;

namespace futures {
namespace detail {

template <typename...>
struct ArgType;

template <typename Arg, typename... Args>
struct ArgType<Arg, Args...> {
  using FirstArg = Arg;
  using Tail = ArgType<Args...>;
};

template <>
struct ArgType<> {
  using FirstArg = void;
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
  using Arg = detail::argResult<true, F, Try<T>&&>;
  using ReturnsFuture = isFutureOrSemiFuture<typename Arg::Result>;
  using value_type = typename ReturnsFuture::Inner;
};

template <typename T, typename F>
struct tryExecutorCallableResult {
  using Arg = detail::argResult<true, F, Executor::KeepAlive<>&&, Try<T>&&>;
  using ReturnsFuture = isFutureOrSemiFuture<typename Arg::Result>;
  using value_type = typename ReturnsFuture::Inner;
};

template <typename T, typename F>
struct valueCallableResult {
  using Arg = detail::argResult<false, F, T&&>;
  using ReturnsFuture = isFutureOrSemiFuture<typename Arg::Result>;
  using value_type = typename ReturnsFuture::Inner;
};

template <typename T, typename F>
struct valueExecutorCallableResult {
  using Arg = detail::argResult<false, F, Executor::KeepAlive<>&&, T&&>;
  using ReturnsFuture = isFutureOrSemiFuture<typename Arg::Result>;
  using value_type = typename ReturnsFuture::Inner;
};

class DeferredExecutor;

} // namespace detail
} // namespace futures

class Timekeeper;

} // namespace folly
