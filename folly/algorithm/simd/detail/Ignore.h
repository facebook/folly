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

#include <folly/lang/Bits.h>

#include <type_traits>

namespace folly::simd::detail {

/**
 * ignore(_none/_extrema)
 *
 * Tag types for handling the tails.
 * ignore_none indicates that the whole register is used.
 * ignore_extrema.first, .last show how many elements are out of the data.
 *
 * For example 3 elements, starting from the second for an 8 element register
 * will be ignore_extrema{.first = 1, .last = 4}
 */

struct ignore_extrema {
  int first = 0;
  int last = 0;
};

struct ignore_none {};

/*
 * NOTE: for ignore none we don't clear anything, even if some bits are not
 * doing anything. We expect mmask to only have zeroes in masked out elements.
 *
 * Maybe we need to revisit that at some point.
 */
template <int Cardinal, typename Uint, typename BitsPerElement, typename Ignore>
void mmaskClearIgnored(std::pair<Uint, BitsPerElement>& mmask, Ignore ignore) {
  if constexpr (std::is_same_v<Ignore, ignore_extrema>) {
    mmask.first = set_rzero(mmask.first, ignore.first * BitsPerElement{});

    static constexpr int kTopBitsAlwaysIgnored =
        sizeof(Uint) * 8 - Cardinal * BitsPerElement{};
    mmask.first = set_lzero(
        mmask.first, ignore.last * BitsPerElement{} + kTopBitsAlwaysIgnored);
  }
}

} // namespace folly::simd::detail
