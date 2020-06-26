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

#pragma once

#include <folly/Traits.h>
#include <glog/logging.h>
#include <utility>

namespace folly {
namespace fibers {
namespace async {

template <typename T>
class Async;

namespace detail {
/**
 * Define in source to avoid including FiberManager header and keep this file
 * cheap to include
 */
bool onFiber();

struct await_fn {
  template <typename T>
  T&& operator()(Async<T>&&) const noexcept;

  void operator()(Async<void>&&) const noexcept {}
};
} // namespace detail

/**
 * Asynchronous fiber result wrapper
 *
 * Syntactic sugar to indicate that can be used as the return type of a
 * function, indicating that a fiber can be preempted within that function.
 * Wraps the eagerly executed result of the function and must be 'await'ed to
 * retrieve the result.
 *
 * Since fibers context switches are implicit, it can be difficult to tell if a
 * function does I/O. In large codebases, it can also be difficult to tell if a
 * given function is running on fibers or not. The Async<> return type makes
 * I/O explicit and provides a good way to identify code paths running on fiber.
 *
 * Async must be combined with static analysis (eg. lints) that forces a
 * function that calls 'await' to also return an Async wrapped result.
 *
 * Runtime Consideration:
 * - The wrapper is currently 0 cost (in optimized builds), and this will
 *   remain a guarentee
 * - It provides protection (in debug builds) against running Async-annotated
 *   code on main context.
 * - It does NOT provide protection against doing I/O in non Async-annotated
 *   code, both asynchronously (on fiber) or blocking (main context).
 * - It does NOT provide protection from fiber's stack overflow.
 */
template <typename T>
class [[nodiscard]] Async {
 public:
  typedef T inner_type;

  // General use constructor
  template <typename... Us>
  /* implicit */ Async(Us && ... val) : val_(std::forward<Us>(val)...) {}

  // Move constructor to allow eager-return of async without using await
  template <typename U>
  /* implicit */ Async(Async<U> && async) noexcept
      : val_(static_cast<U&&>(async.val_)) {}

  Async(const Async&) = delete;
  Async(Async && other) = default;
  Async& operator=(const Async&) = delete;
  Async& operator=(Async&&) = delete;

 private:
  T val_;

  template <typename U>
  friend class Async;

  friend struct detail::await_fn;
};

template <>
class [[nodiscard]] Async<void> {
 public:
  typedef void inner_type;

  /* implicit */ Async() {}
  Async(const Async&) = delete;
  Async(Async && other) = default;
  Async& operator=(const Async&) = delete;
  Async operator=(Async&&) = delete;
};

#if __cpp_deduction_guides >= 201703
/**
 * Deduction guide to make it easier to construct and return Async objects.
 * The guide doesn't permit constructing and returning by reference.
 */
template <typename T>
explicit Async(T)->Async<T>;
#endif

namespace detail {
template <typename T>
T&& await_fn::operator()(Async<T>&& async) const noexcept {
  return static_cast<T&&>(async.val_);
}
} // namespace detail

/**
 * Function to retrieve the result from the Async wrapper
 * A function calling await must return an Async wrapper itself
 * for the wrapper to serve its intended purpose (the best way to enforce this
 * is static analysis)
 */
constexpr detail::await_fn await;

/**
 * A utility to start annotating at top of stack (eg. the task which is added to
 * fiber manager) A function must not return an Async wrapper if it uses
 * `init_await` instead of `await` (again, enforce via static analysis)
 */
template <typename T>
T&& init_await(Async<T>&& async) {
  return await(std::move(async));
}

inline void init_await(Async<void>&& async) {
  await(std::move(async));
}

// is_async
template <typename T>
constexpr bool is_async_v = folly::detail::is_instantiation_of_v<Async, T>;

// async_inner_type
template <typename T>
struct async_inner_type {
  using type = T;
};

template <typename T>
struct async_inner_type<Async<T>> {
  using type = T;
};

template <typename T>
using async_inner_type_t = typename async_inner_type<T>::type;

} // namespace async
} // namespace fibers
} // namespace folly
