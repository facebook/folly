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

#include <folly/detail/SplitStringSimd.h>
#include <folly/detail/SplitStringSimdImpl.h>

#include <folly/FBString.h>
#include <folly/FBVector.h>
#include <folly/small_vector.h>

namespace folly {
namespace detail {

template <typename Container>
void SimdSplitByCharImpl<Container>::keepEmpty(
    char sep, folly::StringPiece what, Container& res) {
  PlatformSimdSplitByChar<
      simd::detail::SimdPlatform<std::uint8_t>,
      /*ignoreEmpty*/ false>{}(sep, what, res);
}

template <typename Container>
void SimdSplitByCharImpl<Container>::dropEmpty(
    char sep, folly::StringPiece what, Container& res) {
  PlatformSimdSplitByChar<
      simd::detail::SimdPlatform<std::uint8_t>,
      /*ignoreEmpty*/ true>{}(sep, what, res);
}

template <typename Container>
void SimdSplitByCharImplToStrings<Container>::keepEmpty(
    char sep, folly::StringPiece what, Container& res) {
  PlatformSimdSplitByChar<
      simd::detail::SimdPlatform<std::uint8_t>,
      /*ignoreEmpty*/ false>{}(sep, what, res);
}

template <typename Container>
void SimdSplitByCharImplToStrings<Container>::dropEmpty(
    char sep, folly::StringPiece what, Container& res) {
  PlatformSimdSplitByChar<
      simd::detail::SimdPlatform<std::uint8_t>,
      /*ignoreEmpty*/ true>{}(sep, what, res);
}

// clang-format off
#define FOLLY_DETAIL_DEFINE_ALL_SIMD_SPLIT_OVERLOADS(...) \
  template struct SimdSplitByCharImpl<std::vector<__VA_ARGS__>>; \
  template struct SimdSplitByCharImpl<folly::fbvector<__VA_ARGS__, std::allocator<__VA_ARGS__>>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 1, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 2, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 3, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 4, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 5, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 6, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 7, void>>; \
  template struct SimdSplitByCharImpl<folly::small_vector<__VA_ARGS__, 8, void>>;
// clang-format on

FOLLY_DETAIL_DEFINE_ALL_SIMD_SPLIT_OVERLOADS(folly::StringPiece)

FOLLY_DETAIL_DEFINE_ALL_SIMD_SPLIT_OVERLOADS(std::string_view)

#undef FOLLY_DETAIL_DEFINE_ALL_SIMD_SPLIT_OVERLOADS

template struct SimdSplitByCharImplToStrings<std::vector<std::string>>;
template struct SimdSplitByCharImplToStrings<std::vector<fbstring>>;
template struct SimdSplitByCharImplToStrings<fbvector<std::string>>;
template struct SimdSplitByCharImplToStrings<fbvector<fbstring>>;
} // namespace detail
} // namespace folly
