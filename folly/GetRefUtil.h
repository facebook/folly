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

#include <concepts>
#include <folly/CppAttributes.h>

namespace folly {

// Concept matching types with optional-like semantics (has_value/value),
// such as std::optional, folly::Optional, and Thrift optional fields.
template <typename T>
concept IsOptionalLike = requires {
  typename T::value_type;
} && requires(const T t) {
  { t.value() } -> std::convertible_to<typename T::value_type>;
  { t.has_value() } -> std::same_as<bool>;
};

// Returns *ptr if non-null, otherwise a const reference to a
// default-constructed static T. Avoids copies compared to a ternary
// that constructs a temporary default.
template <typename T>
const T& getRefOrDefault(const T* FOLLY_NULLABLE ptr) {
  static const T kDefault{};
  return ptr != nullptr ? *ptr : kDefault;
}

// Returns optRef.value() if present, otherwise a const reference to a
// default-constructed static value_type. Avoids the copy that
// value_or({}) would make.
template <IsOptionalLike T>
const typename T::value_type& getRefOrDefault(const T& optRef) {
  static const typename T::value_type kDefault{};
  return optRef.has_value() ? optRef.value() : kDefault;
}

} // namespace folly
