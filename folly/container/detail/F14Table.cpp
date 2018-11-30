/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/container/detail/F14Table.h>

#include <atomic>
#include <chrono>

namespace folly {
namespace f14 {
namespace detail {

// If you get a link failure that leads you here, your build has varying
// compiler flags across compilation units in a way that would break F14.
// SIMD (SSE2 or NEON) needs to be either on everywhere or off everywhere
// that uses F14.  If SIMD is on then hardware CRC needs to be enabled
// everywhere or disabled everywhere.
void F14LinkCheck<getF14IntrinsicsMode()>::check() noexcept {}

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
EmptyTagVectorType kEmptyTagVector = {};
#endif

FOLLY_F14_TLS_IF_ASAN std::size_t asanPendingSafeInserts = 0;

std::size_t tlsMinstdRand(std::size_t n) {
  FOLLY_SAFE_DCHECK(n > 0, "");

#if defined(FOLLY_TLS) && (!defined(NDEBUG) || FOLLY_ASAN_ENABLED)
  static FOLLY_TLS uint32_t state = 0;
#else
  static std::atomic<uint32_t> state{0};
#endif
  uint32_t s = state;
  if (s == 0) {
    uint64_t seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    s = hash::twang_32from64(seed);
  }

  s = static_cast<uint32_t>((s * uint64_t{48271}) % uint64_t{2147483647});
  state = s;
  return std::size_t{s} % n;
}

} // namespace detail
} // namespace f14
} // namespace folly
