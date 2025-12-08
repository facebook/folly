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

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <random>

#include <folly/Likely.h>
#include <folly/Portability.h>

#if FOLLY_X64
#include <immintrin.h>
#endif

namespace folly {

template <typename ResType>
class xoshiro256pp {
 public:
  using result_type = ResType;
  static constexpr result_type default_seed =
      static_cast<result_type>(0x8690c864c6e0b716);

  // While this is not the actual size of the state, it is the size of the input
  // seed that we allow. Any uses of a larger state in the form of a seed_seq
  // will be ignored after the first small part of it.
  static constexpr size_t state_size = sizeof(uint64_t) / sizeof(result_type);
  // Add static_asserts to enforce constraints on ResType
  static_assert(
      std::is_integral_v<result_type>, "ResType must be an integral type.");
  static_assert(
      std::is_unsigned_v<result_type>, "ResType must be an unsigned type.");

  xoshiro256pp(uint64_t pSeed = default_seed) noexcept : state{} {
    seed(pSeed);
  }

  explicit xoshiro256pp(std::seed_seq& seq) noexcept {
    // Initialize the state using the seed sequence.
    uint64_t val;
    seq.generate(&val, &val + 1);
    seed(val);
  }

  result_type operator()() noexcept { return next(); }
  static constexpr result_type min() noexcept {
    return std::numeric_limits<result_type>::min();
  }
  static constexpr result_type max() noexcept {
    return std::numeric_limits<result_type>::max();
  }

  void seed(uint64_t pSeed = default_seed) noexcept {
    uint64_t seed_val = pSeed;
    for (uint64_t result_count = 0; result_count < VecResCount; result_count++) {
      for (uint64_t state_count = 0; state_count < StateSize; state_count++) {
        state[idx(state_count, result_count)] = splitmix64(seed_val);
      }
    }
    cur = ResultCount;
  }

  void seed(std::seed_seq& seq) noexcept {
    // Initialize the state using the seed sequence.
    std::array<uint64_t, 1> seeds{};
    seq.generate(seeds.begin(), seeds.end());
    seed(seeds[0]);
  }

 private:
  using vector_type = uint64_t;
  static constexpr uint64_t StateSize = 4;
  static constexpr uint64_t VecResCount = 16;
  static constexpr uint64_t size_ratio = sizeof(vector_type) / sizeof(result_type);
  static constexpr uint64_t ResultCount = VecResCount * size_ratio;

  vector_type state[StateSize * VecResCount]{};
  
  union {
    vector_type vecRes[VecResCount]{};
    result_type res[ResultCount];
  };
  uint64_t cur = ResultCount;

  template <typename Size, typename CharT, typename Traits>
  friend std::basic_ostream<CharT, Traits>& operator<<(
      std::basic_ostream<CharT, Traits>& os,
      const xoshiro256pp<Size>& rng);

  template <typename T>
  static inline T seed_vec(uint64_t& seed) {
    if constexpr (sizeof(T) != sizeof(uint64_t)) {
      T sbase{};
      for (uint64_t i = 0; i < sizeof(vector_type) / sizeof(uint64_t); i++) {
        sbase[i] = splitmix64(seed);
      }
      return sbase;
    } else {
      return T(splitmix64(seed));
    }
  }

  static inline uint64_t splitmix64(uint64_t& cur) noexcept {
    uint64_t z = (cur += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
  }

  constexpr uint64_t FOLLY_ALWAYS_INLINE idx(uint64_t state_idx, uint64_t result_idx) noexcept {
    return state_idx * VecResCount + result_idx;
  }

  FOLLY_ALWAYS_INLINE static vector_type rotl(
      const vector_type x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }

  void calc() noexcept {
    // By default, the compiler will prefer to unroll the loop completely, deactivating vectorization.
    #if defined(__clang__)
    #pragma clang loop unroll(disable) vectorize_width(8)
    #elif defined(__GNUC__)
    #pragma GCC unroll 4
    #endif
    for (int i = 0; i < VecResCount; ++i) {
      const vector_type vec_res = rotl(state[idx(0, i)] + state[idx(3, i)], 23) + state[idx(0, i)];
      std::memcpy(&res[i * size_ratio], &vec_res, sizeof(vector_type));
      
      const auto t = state[idx(1, i)] << 17;
      state[idx(2, i)] ^= state[idx(0, i)];
      state[idx(3, i)] ^= state[idx(1, i)];
      state[idx(1, i)] ^= state[idx(2, i)];
      state[idx(0, i)] ^= state[idx(3, i)];
      state[idx(2, i)] ^= t;
      state[idx(3, i)] = rotl(state[idx(3, i)], 45);
    }
    cur = 0;
  }

  FOLLY_ALWAYS_INLINE result_type next() noexcept {
    if (FOLLY_UNLIKELY(cur == ResultCount)) {
      calc();
    }
    return res[cur++];
  }
};

template <typename Size, typename CharT, typename Traits>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,
    const xoshiro256pp<Size>& rng) {
  for (auto i2 : rng.res) {
    os << i2 << " ";
  }
  os << "cur: " << rng.cur;
  return os;
}

using xoshiro256pp_32 = xoshiro256pp<uint32_t>;
using xoshiro256pp_64 = xoshiro256pp<uint64_t>;

} // namespace folly
