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

#include <cstdint>
#include <cstdlib>

#include <folly/Portability.h>

namespace folly {
namespace hash {
namespace detail {

// A non-deterministic seed to prevent users depend on particular values of the
// hash. Based on the same idea as Google's Abseil implementation [1]:
// leverage ASLR (Address Space Layout Randomization) [2] to get a per-run
// random value.
//
// [1]:
// https://github.com/abseil/abseil-cpp/blob/76fd1e96c71ad20fe08f1b7d18c6c55e197df85e/absl/hash/internal/hash.h#L1299-L1323
// [2]: https://en.wikipedia.org/wiki/Address_space_layout_randomization
class RandomSeed {
 public:
  static uint64_t seed() {
    // Abseil's implementation takes an address of static variable. It works
    // fine, as long as hash values doesn't cross shared object/binary
    // boundary. Unfortunately, we observed few such cases, so we are taking
    // address of `std::abort` instead to increase chances of having single
    // address across multiple shared objects.
    return kIsDebug
        ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&std::abort))
        : 0ULL;
  }
};

} // namespace detail
} // namespace hash
} // namespace folly
