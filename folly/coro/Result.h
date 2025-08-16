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
#include <folly/coro/Error.h> // compat: used to be the same header
#include <folly/result/result.h>

namespace folly::coro {

template <typename T>
class co_result final {
 public:
  explicit co_result(Try<T>&& result) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : result_(std::move(result)) {
    assert(!result_.hasException() || result_.exception());
  }

#if FOLLY_HAS_RESULT
  // Covered in `AwaitResultTest.cpp`, unlike the rest of this file, which is
  // covered in `TaskTest.cpp`.
  template <std::same_as<folly::result<T>> U> // no implicit ctors for `result`
  explicit co_result(U result) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : co_result(result_to_try(std::move(result))) {}
#endif

  const Try<T>& result() const { return result_; }

  Try<T>& result() { return result_; }

 private:
  Try<T> result_;
};

#if FOLLY_HAS_RESULT
template <typename T>
co_result(result<T>) -> co_result<T>;
#endif

} // namespace folly::coro
