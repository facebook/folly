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

#include <exception>

namespace folly {
namespace detail {

//  thunk
//
//  A carefully curated collection of generic general-purpose thunk templates:
//  * make: operator new with given arguments
//  * ruin: operator delete
//  * ctor: in-place constructor with given arguments
//  * dtor: in-place destructor
//  * noop: no-op function with the given arguments
//  * fail: terminating function with the given return and arguments
struct thunk {
  template <typename T, typename... A>
  static void* make(A... a) {
    return new T(static_cast<A>(a)...);
  }
  template <typename T>
  static void ruin(void* const ptr) noexcept {
    delete static_cast<T*>(ptr);
  }

  template <typename T, typename... A>
  static void ctor(void* const ptr, A... a) {
    ::new (ptr) T(static_cast<A>(a)...);
  }
  template <typename T>
  static void dtor(void* const ptr) noexcept {
    static_cast<T*>(ptr)->~T();
  }

  template <typename... A>
  static void noop(A...) noexcept {}

  template <typename R, typename... A>
  static R fail(A...) noexcept {
    std::terminate();
  }
};

} // namespace detail
} // namespace folly
