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

#include <folly/coro/Coroutine.h> // delete with clang-15 support
#include <folly/result/result.h>

#if FOLLY_HAS_RESULT

#include <coroutine>

namespace folly {
namespace detail {

template <typename>
struct result_promise_base;

template <typename T>
struct result_promise_return {
  result<T> storage_{expected_detail::EmptyTag{}};
  result<T>*& pointer_;

  /* implicit */ result_promise_return(result_promise_base<T>& p) noexcept
      : pointer_{p.value_} {
    pointer_ = &storage_;
  }
  result_promise_return(result_promise_return const&) = delete;
  void operator=(result_promise_return const&) = delete;
  result_promise_return(result_promise_return&&) = delete;
  void operator=(result_promise_return&&) = delete;
  // Remove this once clang 15 is well-forgotten. From D42260201:
  // letting dtor be trivial makes the coroutine crash
  ~result_promise_return() {}

  /* implicit */ operator result<T>() {
    // Simplify this once clang 15 is well-forgotten, and remove the dep on
    // `Coroutine.h`.  From D42260201: handle both deferred and eager
    // return-object conversion behaviors see docs for
    // detect_promise_return_object_eager_conversion
    if (coro::detect_promise_return_object_eager_conversion()) {
      assert(storage_.is_expected_empty());
      return result<T>{expected_detail::EmptyTag{}, pointer_}; // eager
    } else {
      assert(!storage_.is_expected_empty());
      return std::move(storage_); // deferred
    }
  }
};

template <typename T>
struct result_promise_base {
  result<T>* value_ = nullptr;

  result_promise_base() = default;
  result_promise_base(result_promise_base const&) = delete;
  void operator=(result_promise_base const&) = delete;
  result_promise_base(result_promise_base&&) = delete;
  void operator=(result_promise_base&&) = delete;
  ~result_promise_base() = default;

  std::suspend_never initial_suspend() const noexcept { return {}; }
  std::suspend_never final_suspend() const noexcept { return {}; }
  void unhandled_exception() noexcept {
    *value_ = non_value_result::from_current_exception();
  }

  result_promise_return<T> get_return_object() noexcept { return *this; }
};

template <typename T>
struct result_promise<T, typename std::enable_if<!std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // For reference types, this deliberately requires users to `co_return`
  // one of `std::ref`, `std::cref`, or `folly::rref`.
  //
  // The default for `U` is tested in `returnImplicitCtor`.
  template <typename U = T>
  void return_value(U&& u) {
    auto& v = *this->value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    v = static_cast<U&&>(u);
  }
};

template <typename T>
struct result_promise<T, typename std::enable_if<std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // You can fail via `co_await err` since void coros only allow `co_return;`.
  void return_void() { this->value_->exp_.emplace(unit); }
};

template <typename T> // Need an alias since nested types cannot be deduced.
using result_promise_handle = std::coroutine_handle<result_promise<T>>;

} // namespace detail
} // namespace folly

#endif // FOLLY_HAS_RESULT
