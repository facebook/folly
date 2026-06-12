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

#include <cassert>
#include <type_traits>

#include <folly/Try.h>
#include <folly/Utility.h>
#include <folly/coro/Error.h> // compat: used to be the same header
#include <folly/lang/MustUseImmediately.h>
#include <folly/result/try.h>
#include <folly/result/value_only_result.h>

namespace folly::coro {

namespace detail {

template <typename Promise, typename T>
class TaskPromiseCrtpBase;

template <typename T>
class TaskPromise;

template <typename Reference, typename Value, bool RequiresCleanup>
class AsyncGeneratorPromise;

} // namespace detail

template <typename T, typename ContainerRef = Try<T>&&>
  requires(
      std::is_same_v<std::remove_cvref_t<ContainerRef>, Try<T>>
#if FOLLY_HAS_RESULT
      || std::is_same_v<std::remove_cvref_t<ContainerRef>, folly::result<T>> ||
      std::is_same_v<
          std::remove_cvref_t<ContainerRef>,
          folly::value_only_result<T>>
#endif
      )
class co_result final
    : public folly::ext::must_use_immediately_crtp<co_result<T, ContainerRef>> {
  using Container = std::remove_cvref_t<ContainerRef>;

 public:
  explicit co_result(ContainerRef result) noexcept
      : result_(static_cast<ContainerRef>(result)) {
    if constexpr (std::is_same_v<Container, Try<T>>) {
      assert(!result_.hasException() || result_.exception());
    }
  }

 private:
  // Only for `assignTo()`
  template <typename Promise, typename U>
  friend class detail::TaskPromiseCrtpBase;

  // Only for `assignTo()` in `Task<void>`
  template <typename U>
  friend class detail::TaskPromise;

  // Only for `consumeIntoGenerator()`
  template <typename Reference, typename Value, bool RequiresCleanup>
  friend class detail::AsyncGeneratorPromise;

 public:
  class unsafe_mover_t {
   public:
    explicit unsafe_mover_t(ContainerRef result) noexcept
        : result_(static_cast<ContainerRef>(result)) {}

    co_result operator()() && noexcept {
      return co_result(static_cast<ContainerRef>(result_));
    }

   private:
    ContainerRef result_;
  };

  static unsafe_mover_t unsafe_mover(
      folly::ext::must_use_immediately_private_t /*unused*/,
      co_result&& me) noexcept {
    return unsafe_mover_t(static_cast<ContainerRef>(me.result_));
  }

 private:
  void assignTo(Try<T>& out) && {
    if constexpr (std::is_same_v<Container, Try<T>>) {
      out = static_cast<ContainerRef>(result_);
    } else {
#if FOLLY_HAS_RESULT
      if constexpr (std::is_same_v<Container, folly::result<T>>) {
        out = result_to_try(static_cast<ContainerRef>(result_));
      } else {
        static_assert(std::is_same_v<Container, folly::value_only_result<T>>);
        // `value_only_result` cannot carry errors.
        if constexpr (std::is_void_v<T>) {
          out.emplace();
        } else {
          out.emplace(static_cast<ContainerRef>(result_).value_or_throw());
        }
      }
#endif
    }
  }

  // Legacy `Unit`-as-void bridge for `Task<void>`
  void assignTo(Try<void>& out) &&
    requires std::is_same_v<T, Unit>
  {
    if constexpr (std::is_same_v<Container, Try<Unit>>) {
      if (result_.hasException()) {
        out.emplaceException(static_cast<ContainerRef>(result_).exception());
      } else {
        out.emplace();
      }
    } else {
#if FOLLY_HAS_RESULT
      if constexpr (std::is_same_v<Container, folly::result<Unit>>) {
        if (result_.has_value()) {
          out.emplace();
        } else {
          out.emplaceException(
              folly::copy(static_cast<ContainerRef>(result_).error_or_stopped())
                  .get_legacy_error_or_cancellation_slow(
                      folly::detail::result_private_t{}));
        }
      } else {
        static_assert(
            std::is_same_v<Container, folly::value_only_result<Unit>>);
        out.emplace();
      }
#endif
    }
  }

  // Decode the input result object without materializing an owning Try<T>.
  template <typename ValueFn, typename ExceptionFn, typename EmptyTryFn>
  decltype(auto) consumeIntoGenerator(
      ValueFn&& valueFn,
      ExceptionFn&& exceptionFn,
      EmptyTryFn&& emptyTryFn) && {
    if constexpr (std::is_same_v<Container, Try<T>>) {
      if (result_.hasValue()) {
        return static_cast<ValueFn&&>(valueFn)(
            static_cast<ContainerRef>(result_).value());
      } else if (result_.hasException()) {
        return static_cast<ExceptionFn&&>(exceptionFn)(
            static_cast<ContainerRef>(result_).exception());
      } else {
        return static_cast<EmptyTryFn&&>(emptyTryFn)();
      }
#if FOLLY_HAS_RESULT
    } else if constexpr (std::is_same_v<Container, folly::result<T>>) {
      if (result_.has_value()) {
        return static_cast<ValueFn&&>(valueFn)(
            static_cast<ContainerRef>(result_).value_or_throw());
      } else {
        return static_cast<ExceptionFn&&>(exceptionFn)(
            folly::copy(static_cast<ContainerRef>(result_).error_or_stopped())
                .get_legacy_error_or_cancellation_slow(
                    folly::detail::result_private_t{}));
      }
    } else {
      static_assert(std::is_same_v<Container, folly::value_only_result<T>>);
      return static_cast<ValueFn&&>(valueFn)(
          static_cast<ContainerRef>(result_).value_or_throw());
#endif
    }
  }

  ContainerRef result_;
};

template <typename T>
co_result(Try<T>&&) -> co_result<T, Try<T>&&>;
template <typename T>
co_result(Try<T>&) -> co_result<T, const Try<T>&>;
template <typename T>
co_result(const Try<T>&) -> co_result<T, const Try<T>&>;
#if FOLLY_HAS_RESULT
template <typename T>
co_result(result<T>&&) -> co_result<T, result<T>&&>;
template <typename T>
co_result(result<T>&) -> co_result<T, const result<T>&>;
template <typename T>
co_result(const result<T>&) -> co_result<T, const result<T>&>;
template <typename T>
co_result(value_only_result<T>&&) -> co_result<T, value_only_result<T>&&>;
template <typename T>
co_result(value_only_result<T>&) -> co_result<T, const value_only_result<T>&>;
template <typename T>
co_result(const value_only_result<T>&)
    -> co_result<T, const value_only_result<T>&>;
#endif

} // namespace folly::coro
