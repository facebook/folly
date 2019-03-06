/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

namespace folly {
namespace detail {

// The StaticConst<T> class is a helper for defining constexpr objects at
// namespace scope in an ODR-safe and initialisation-order fiasco-safe way
// in C++ versions earlier than C++17.
//
// For example, see the FOLLY_DEFINE_CPO() macro in folly/Portability.h for
// usage in defining customisation-point objects (CPOs).

template <typename T>
struct StaticConst {
  static constexpr T value{};
};

template <typename T>
constexpr T StaticConst<T>::value;

} // namespace detail
} // namespace folly
