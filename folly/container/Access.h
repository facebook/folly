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

#include <initializer_list>
#include <iterator>

#include <folly/Portability.h>
#include <folly/functional/Invoke.h>

namespace folly {

namespace access {

//  mimic: std::size, C++17
struct size_fn {
  template <typename C>
  FOLLY_ERASE constexpr auto operator()(C const& c) const
      noexcept(noexcept(c.size())) -> decltype(c.size()) {
    return c.size();
  }
  template <typename T, std::size_t N>
  FOLLY_ERASE constexpr std::size_t operator()(T const (&)[N]) const noexcept {
    return N;
  }
};
FOLLY_INLINE_VARIABLE constexpr size_fn size{};

//  mimic: std::empty, C++17
struct empty_fn {
  template <typename C>
  FOLLY_ERASE constexpr auto operator()(C const& c) const
      noexcept(noexcept(c.empty())) -> decltype(c.empty()) {
    return c.empty();
  }
  template <typename T, std::size_t N>
  FOLLY_ERASE constexpr bool operator()(T const (&)[N]) const noexcept {
    //  while zero-length arrays are not allowed in the language, some compilers
    //  may permit them in some cases
    return N == 0;
  }
  template <typename E>
  FOLLY_ERASE constexpr bool operator()(
      std::initializer_list<E> il) const noexcept {
    return il.size() == 0;
  }
};
FOLLY_INLINE_VARIABLE constexpr empty_fn empty{};

//  mimic: std::data, C++17
struct data_fn {
  template <typename C>
  FOLLY_ERASE constexpr auto operator()(C& c) const noexcept(noexcept(c.data()))
      -> decltype(c.data()) {
    return c.data();
  }
  template <typename C>
  FOLLY_ERASE constexpr auto operator()(C const& c) const
      noexcept(noexcept(c.data())) -> decltype(c.data()) {
    return c.data();
  }
  template <typename T, std::size_t N>
  FOLLY_ERASE constexpr T* operator()(T (&a)[N]) const noexcept {
    return a;
  }
  template <typename E>
  FOLLY_ERASE constexpr E const* operator()(
      std::initializer_list<E> il) const noexcept {
    return il.begin();
  }
};
FOLLY_INLINE_VARIABLE constexpr data_fn data{};

//  begin
//
//  Invokes unqualified begin with std::begin in scope.
FOLLY_CREATE_FREE_INVOKER(begin_fn, begin, std);
FOLLY_INLINE_VARIABLE constexpr begin_fn begin{};

//  end
//
//  Invokes unqualified end with std::end in scope.
FOLLY_CREATE_FREE_INVOKER(end_fn, end, std);
FOLLY_INLINE_VARIABLE constexpr end_fn end{};

} // namespace access

} // namespace folly
