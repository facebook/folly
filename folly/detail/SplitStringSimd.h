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

#include <folly/Range.h>
#include <folly/detail/SplitStringSimdImpl.h>

namespace folly {
namespace detail {

template <typename Container>
void simdSplitByChar(
    char sep, folly::StringPiece what, Container& res, bool ignoreEmpty) {
  using Platform = simd::detail::SimdPlatform<std::uint8_t>;
  if (ignoreEmpty) {
    PlatformSimdSplitByChar<
        Platform,
        /*ignoreEmpty*/ true>{}(sep, what, res);
  } else {
    PlatformSimdSplitByChar<
        Platform,
        /*ignoreEmpty*/ false>{}(sep, what, res);
  }
}

} // namespace detail
} // namespace folly
