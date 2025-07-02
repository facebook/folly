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

#include <folly/Try.h>
#include <folly/result/result.h>

/// `result<T>` <-> `Try<T>` conversions to aid in migrating legacy `Try` code.
///
/// Perfect interconversion is not always possible:
///   - `Try` does not support reference types, but `result` does.
///   - `Try` has 3 states.  Value & error interconvert transparently.  For the
///     empty state, `try_to_result` has a 2nd arg to set the policy.

#if FOLLY_HAS_RESULT

namespace folly {

// NB: If `T` is a reference type, this will fail with a `Try` static assert.
template <typename T>
Try<T> result_to_try(result<T> r) noexcept(
    std::is_nothrow_move_constructible_v<T>) {
  if (r.has_value()) {
    if constexpr (std::is_void_v<T>) {
      return Try<void>{};
    } else {
      return Try<T>{std::move(r).value_or_throw()};
    }
  } else {
    return Try<T>{std::move(r).non_value().get_legacy_error_or_cancellation()};
  }
}

inline constexpr struct empty_try_as_error_t {
  template <typename T>
  result<T> on_empty_try() const {
    return {non_value_result{UsingUninitializedTry{}}};
  }
} empty_try_as_error;

template <typename Fn>
class empty_try_with {
 private:
  Fn fn_;

 public:
  explicit empty_try_with(Fn fn) : fn_(std::move(fn)) {}
  template <typename T>
  result<T> on_empty_try() && {
    return {std::move(fn_)()};
  }
};

/// If `t` is empty, defaults to returning a `UsingUninitializedTry` result.
/// Pick your own default via `empty_try_with{[]() { return result<T>{...}; }`.
template <typename T, typename IfEmpty>
result<T> try_to_result(Try<T> t, IfEmpty if_empty) noexcept(
    std::is_nothrow_move_constructible_v<T> &&
    noexcept(std::move(if_empty).template on_empty_try<T>())) {
  if (t.hasValue()) {
    if constexpr (std::is_void_v<T>) {
      return result<void>{};
    } else {
      return {std::move(t).value()};
    }
  } else if (t.hasException()) {
    return {non_value_result::make_legacy_error_or_cancellation(
        std::move(t).exception())};
  } else {
    return std::move(if_empty).template on_empty_try<T>();
  }
}
template <typename T>
result<T> try_to_result(Try<T> t) noexcept(
    std::is_nothrow_move_constructible_v<T>) {
  return try_to_result(std::move(t), empty_try_as_error);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
