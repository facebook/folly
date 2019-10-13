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

#include <memory>
#include <type_traits>

namespace folly {
namespace coro {
namespace detail {

// Helper class for a variable with manually-controlled lifetime.
//
// You must explicitly call .construct() to construct/initialise the value.
//
// If it has been initialised then you must explicitly call .destruct() before
// the ManualLifetime object is destroyed to ensure the destructor is run.
template <typename T>
class ManualLifetime {
 public:
  ManualLifetime() noexcept {}
  ~ManualLifetime() {}

  template <
      typename... Args,
      std::enable_if_t<std::is_constructible<T, Args...>::value, int> = 0>
  void construct(Args&&... args) noexcept(
      noexcept(std::is_nothrow_constructible<T, Args...>::value)) {
    new (static_cast<void*>(std::addressof(value_)))
        T(static_cast<Args&&>(args)...);
  }

  void destruct() noexcept {
    value_.~T();
  }

  const T& get() const& {
    return value_;
  }
  T& get() & {
    return value_;
  }
  const T&& get() const&& {
    return static_cast<const T&&>(value_);
  }
  T&& get() && {
    return static_cast<T&&>(value_);
  }

 private:
  union {
    std::remove_const_t<T> value_;
  };
};

template <typename T>
class ManualLifetime<T&> {
 public:
  ManualLifetime() noexcept : ptr_(nullptr) {}
  ~ManualLifetime() {}

  void construct(T& value) noexcept {
    ptr_ = std::addressof(value);
  }

  void destruct() noexcept {
    ptr_ = nullptr;
  }

  T& get() const noexcept {
    return *ptr_;
  }

 private:
  T* ptr_;
};

template <typename T>
class ManualLifetime<T&&> {
 public:
  ManualLifetime() noexcept : ptr_(nullptr) {}
  ~ManualLifetime() {}

  void construct(T&& value) noexcept {
    ptr_ = std::addressof(value);
  }

  void destruct() noexcept {
    ptr_ = nullptr;
  }

  T&& get() const noexcept {
    return static_cast<T&&>(*ptr_);
  }

 private:
  T* ptr_;
};

template <>
class ManualLifetime<void> {
 public:
  void construct() noexcept {}

  void destruct() noexcept {}

  void get() const noexcept {}
};

} // namespace detail
} // namespace coro
} // namespace folly
