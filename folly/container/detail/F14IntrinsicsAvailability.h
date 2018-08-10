/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/Portability.h>

// F14 has been implemented for SSE2 and NEON (so far)
#if FOLLY_SSE >= 2 || FOLLY_NEON
#define FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE 1
#else
#define FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE 0
#pragma message                                                      \
    "Vector intrinsics / F14 support unavailable on this platform, " \
    "falling back to std::unordered_map / set"
#endif

#if FOLLY_SSE_PREREQ(4, 2) || __ARM_FEATURE_CRC32
#define FOLLY_F14_CRC_INTRINSIC_AVAILABLE 1
#else
#define FOLLY_F14_CRC_INTRINSIC_AVAILABLE 0
#endif

namespace folly {
namespace f14 {
namespace detail {

enum class F14IntrinsicsMode { None, Simd, SimdAndCrc };

static constexpr F14IntrinsicsMode getF14IntrinsicsMode() {
#if !FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
  return F14IntrinsicsMode::None;
#elif !FOLLY_F14_CRC_INTRINSIC_AVAILABLE
  return F14IntrinsicsMode::Simd;
#else
  return F14IntrinsicsMode::SimdAndCrc;
#endif
}

} // namespace detail
} // namespace f14
} // namespace folly
