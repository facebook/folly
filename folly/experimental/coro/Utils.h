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

#include <experimental/coroutine>
#include <type_traits>

namespace folly {
namespace coro {

template <typename T>
class AwaitableReady {
 public:
  explicit AwaitableReady(T value) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : value_(static_cast<T&&>(value)) {}

  bool await_ready() noexcept {
    return true;
  }

  void await_suspend(std::experimental::coroutine_handle<>) noexcept {}

  T await_resume() noexcept(std::is_nothrow_move_constructible<T>::value) {
    return static_cast<T&&>(value_);
  }

 private:
  T value_;
};

template <>
class AwaitableReady<void> {
 public:
  AwaitableReady() noexcept = default;
  bool await_ready() noexcept {
    return true;
  }
  void await_suspend(std::experimental::coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace coro
} // namespace folly
