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

#include <folly/Traits.h>

namespace folly {
namespace coro {
namespace detail {

/**
 * A type trait that lifts lvalue references into std::reference_wrapper<T>
 * eg. so the value can be stored in std::optional or folly::Try.
 */
template <typename T>
struct lift_lvalue_reference {
  using type = T;
};

template <typename T>
struct lift_lvalue_reference<T&> {
  using type = std::reference_wrapper<T>;
};

template <typename T>
using lift_lvalue_reference_t = typename lift_lvalue_reference<T>::type;

/**
 * A type trait to decay rvalue-reference types to a prvalue.
 */
template <typename T>
struct decay_rvalue_reference {
  using type = T;
};

template <typename T>
struct decay_rvalue_reference<T&&> : remove_cvref<T> {};

template <typename T>
using decay_rvalue_reference_t = typename decay_rvalue_reference<T>::type;

} // namespace detail
} // namespace coro
} // namespace folly
