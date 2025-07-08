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

#include <type_traits>
#include <utility>

#include <folly/functional/Invoke.h>

namespace folly {
namespace dptr_detail {

// Determine the result type of applying a visitor of type V on pointers of
// all types in Types..., asserting that the type is the same for all types
// in Types...
template <typename V, typename... Types>
struct VisitorResult {
  template <typename T>
  using res = invoke_result_t<V, T*>;
  using type = std::common_type_t<res<Types>...>;
  static_assert((std::is_same_v<type, res<Types>> && ...));
};

// Determine the result type of applying a visitor of type V on const pointers
// of all types in Types..., asserting that the type is the same for all types
// in Types...
template <typename V, typename... Types>
struct ConstVisitorResult {
  template <typename T>
  using res = invoke_result_t<V, const T*>;
  using type = std::common_type_t<res<Types>...>;
  static_assert((std::is_same_v<type, res<Types>> && ...));
};

template <size_t index, typename V, typename R, typename... Types>
struct ApplyVisitor1;

template <typename V, typename R, typename T, typename... Types>
struct ApplyVisitor1<1, V, R, T, Types...> {
  R operator()(size_t, V&& visitor, void* ptr) const {
    return visitor(static_cast<T*>(ptr));
  }
};

template <size_t index, typename V, typename R, typename T, typename... Types>
struct ApplyVisitor1<index, V, R, T, Types...> {
  R operator()(size_t runtimeIndex, V&& visitor, void* ptr) const {
    return runtimeIndex == 1
        ? visitor(static_cast<T*>(ptr))
        : ApplyVisitor1<index - 1, V, R, Types...>()(
              runtimeIndex - 1, std::forward<V>(visitor), ptr);
  }
};

template <size_t index, typename V, typename R, typename... Types>
struct ApplyConstVisitor1;

template <typename V, typename R, typename T, typename... Types>
struct ApplyConstVisitor1<1, V, R, T, Types...> {
  R operator()(size_t, V&& visitor, void* ptr) const {
    return visitor(static_cast<const T*>(ptr));
  }
};

template <size_t index, typename V, typename R, typename T, typename... Types>
struct ApplyConstVisitor1<index, V, R, T, Types...> {
  R operator()(size_t runtimeIndex, V&& visitor, void* ptr) const {
    return runtimeIndex == 1
        ? visitor(static_cast<const T*>(ptr))
        : ApplyConstVisitor1<index - 1, V, R, Types...>()(
              runtimeIndex - 1, std::forward<V>(visitor), ptr);
  }
};

template <typename V, typename... Types>
using ApplyVisitor = ApplyVisitor1<
    sizeof...(Types),
    V,
    typename VisitorResult<V, Types...>::type,
    Types...>;

template <typename V, typename... Types>
using ApplyConstVisitor = ApplyConstVisitor1<
    sizeof...(Types),
    V,
    typename ConstVisitorResult<V, Types...>::type,
    Types...>;

} // namespace dptr_detail
} // namespace folly
