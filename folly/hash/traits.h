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

#include <functional>
#include <type_traits>

#include <folly/Traits.h>

namespace folly {

/**
 * Checks the requirements that the Hasher class must satisfy
 * in order to be used with the standard library containers,
 * for example `std::unordered_set<T, Hasher>`.
 */
template <typename T, typename Hasher>
using is_hasher_usable = std::integral_constant<
    bool,
    std::is_default_constructible_v<Hasher> &&
        std::is_copy_constructible_v<Hasher> &&
        std::is_move_constructible_v<Hasher> &&
        std::is_invocable_r_v<size_t, Hasher, const T&>>;

/**
 * Checks the requirements that the Hasher class must satisfy
 * in order to be used with the standard library containers,
 * for example `std::unordered_set<T, Hasher>`.
 */
template <typename T, typename Hasher>
inline constexpr bool is_hasher_usable_v = is_hasher_usable<T, Hasher>::value;

/**
 * Checks that the given hasher template's specialization for the given type
 * is usable with the standard library containters,
 * for example `std::unordered_set<T, Hasher<T>>`.
 */
template <typename T, template <typename U> typename Hasher = std::hash>
using is_hashable =
    std::integral_constant<bool, is_hasher_usable_v<T, Hasher<T>>>;

/**
 * Checks that the given hasher template's specialization for the given type
 * is usable with the standard library containters,
 * for example `std::unordered_set<T, Hasher<T>>`.
 */
template <typename T, template <typename U> typename Hasher = std::hash>
inline constexpr bool is_hashable_v = is_hashable<T, Hasher>::value;

namespace detail {

template <typename T, typename>
using enable_hasher_helper_impl = T;

} // namespace detail

/**
 * A helper for defining partial specializations of a hasher class that rely
 * on other partial specializations of that hasher class being usable.
 *
 * Example:
 * ```
 * template <typename T>
 * struct hash<
 *     folly::enable_std_hash_helper<folly::Optional<T>, remove_const_t<T>>> {
 *   size_t operator()(folly::Optional<T> const& obj) const {
 *     return static_cast<bool>(obj) ? hash<remove_const_t<T>>()(*obj) : 0;
 *   }
 * };
 * ```
 */
template <
    typename T,
    template <typename U>
    typename Hasher,
    typename... Dependencies>
using enable_hasher_helper = detail::enable_hasher_helper_impl<
    T,
    std::enable_if_t<
        StrictConjunction<is_hashable<Dependencies, Hasher>...>::value>>;

/**
 * A helper for defining partial specializations of a hasher class that rely
 * on other partial specializations of that hasher class being usable.
 *
 * Example:
 * ```
 * template <typename T>
 * struct hash<
 *     folly::enable_std_hash_helper<folly::Optional<T>, remove_const_t<T>>> {
 *   size_t operator()(folly::Optional<T> const& obj) const {
 *     return static_cast<bool>(obj) ? hash<remove_const_t<T>>()(*obj) : 0;
 *   }
 * };
 * ```
 */
template <typename T, typename... Dependencies>
using enable_std_hash_helper =
    enable_hasher_helper<T, std::hash, Dependencies...>;

} // namespace folly
