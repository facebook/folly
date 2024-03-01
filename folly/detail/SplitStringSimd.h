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

#include <string>
#include <vector>
#include <folly/Range.h>

namespace folly {

template <typename T, std::size_t M, typename P>
class small_vector;

template <typename T, typename Allocator>
class fbvector;

template <class Char>
class fbstring_core;

template <typename E, typename T, typename A, typename Storage>
class basic_fbstring;

namespace detail {

using PredeclareFbString = basic_fbstring<
    char,
    std::char_traits<char>,
    std::allocator<char>,
    fbstring_core<char>>;

template <typename Container>
struct SimdSplitByCharImpl {
  static void keepEmpty(char sep, folly::StringPiece what, Container& res);
  static void dropEmpty(char sep, folly::StringPiece what, Container& res);
};

// Different name to easier identify in the stack potential performance issues
template <typename Container>
struct SimdSplitByCharImplToStrings {
  static void keepEmpty(char sep, folly::StringPiece what, Container& res);
  static void dropEmpty(char sep, folly::StringPiece what, Container& res);
};

template <typename T>
constexpr bool isSimdSplitSupportedStringViewType =
    std::is_same<T, folly::StringPiece>::value ||
    std::is_same<T, std::string_view>::value;

template <typename T>
constexpr bool isSimdSplitSupportedStringType =
    std::is_same<T, PredeclareFbString>::value ||
    std::is_same<T, std::string>::value;

template <typename>
struct SimdSplitByCharIsDefinedFor {
  static constexpr bool value = false;
};

template <typename T>
struct SimdSplitByCharIsDefinedFor<std::vector<T>> {
  static constexpr bool value = isSimdSplitSupportedStringViewType<T> ||
      isSimdSplitSupportedStringType<T>;
};

template <typename T, typename A>
struct SimdSplitByCharIsDefinedFor<folly::fbvector<T, A>>
    : SimdSplitByCharIsDefinedFor<std::vector<T, A>> {};

template <typename T, std::size_t M>
struct SimdSplitByCharIsDefinedFor<folly::small_vector<T, M, void>> {
  static constexpr bool value =
      isSimdSplitSupportedStringViewType<T> && 0 < M && M <= 8;
};

template <typename Container>
std::enable_if_t<
    isSimdSplitSupportedStringViewType<typename Container::value_type>>
simdSplitByChar(
    char sep, folly::StringPiece what, Container& res, bool ignoreEmpty) {
  static_assert(
      SimdSplitByCharIsDefinedFor<Container>::value,
      "simd split by char is supported only for vector/fbvector/small_vector, with small size <= 8."
      " The resulting string type has to string_view or StringPiece."
      " There is also a special case of (fb)vector<(fb)string> for legacy compatibility");
  if (ignoreEmpty) {
    SimdSplitByCharImpl<Container>::dropEmpty(sep, what, res);
  } else {
    SimdSplitByCharImpl<Container>::keepEmpty(sep, what, res);
  }
}

template <typename Container>
std::enable_if_t<isSimdSplitSupportedStringType<typename Container::value_type>>
simdSplitByChar(
    char sep, folly::StringPiece what, Container& res, bool ignoreEmpty) {
  static_assert(
      SimdSplitByCharIsDefinedFor<Container>::value,
      "simd split by char is supported only for vector/fbvector/small_vector, with small size <= 8."
      " The resulting string type has to string_view or StringPiece."
      " There is also a special case of (fb)vector<(fb)string> for legacy compatibility");
  if (ignoreEmpty) {
    SimdSplitByCharImplToStrings<Container>::dropEmpty(sep, what, res);
  } else {
    SimdSplitByCharImplToStrings<Container>::keepEmpty(sep, what, res);
  }
}

// clang-format off
#define FOLLY_DETAIL_DECLARE_ALL_SIMD_SPLIT_OVERLOADS(...) \
  extern template struct SimdSplitByCharImpl<std::vector<__VA_ARGS__>>; \
  extern template struct SimdSplitByCharImpl<folly::fbvector<__VA_ARGS__, std::allocator<__VA_ARGS__>>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 1, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 2, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 3, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 4, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 5, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 6, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 7, void>>; \
  extern template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 8, void>>;
// clang-format on

FOLLY_DETAIL_DECLARE_ALL_SIMD_SPLIT_OVERLOADS(folly::StringPiece)

FOLLY_DETAIL_DECLARE_ALL_SIMD_SPLIT_OVERLOADS(std::string_view)

extern template struct SimdSplitByCharImplToStrings<std::vector<std::string>>;
extern template struct SimdSplitByCharImplToStrings<
    std::vector<PredeclareFbString>>;
extern template struct SimdSplitByCharImplToStrings<
    fbvector<std::string, std::allocator<std::string>>>;
extern template struct SimdSplitByCharImplToStrings<
    fbvector<PredeclareFbString, std::allocator<PredeclareFbString>>>;

#undef FOLLY_DETAIL_DECLARE_ALL_SIMD_SPLIT_OVERLOADS

} // namespace detail
} // namespace folly
