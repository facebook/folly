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
#include <limits>
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
  static constexpr result_type default_seed = 0x8690c864c6e0b716;

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
    uint64_t seed = pSeed;
    for (uint64_t re = 0; re < VecResCount; re++) {
      for (uint64_t stat = 0; stat < StateSize; stat++) {
        state[re][stat] = seed_vec<vector_type>(seed);
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
#if defined(__AVX2__) && defined(__GNUC__)
  using vector_type = __v4du; // GCC-specific unsigned vector type
#else
  using vector_type = uint64_t; // Fallback for other compilers
#endif
  static constexpr uint64_t StateSize = 4;
  static constexpr uint64_t VecResCount = 8;
  static constexpr uint64_t ResultCount =
      VecResCount * (sizeof(vector_type) / sizeof(result_type));
  union {
    vector_type vecRes[VecResCount]{};
    result_type res[ResultCount];
  };
  vector_type state[VecResCount][StateSize]{};
  uint64_t cur = ResultCount;

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

  FOLLY_ALWAYS_INLINE static vector_type rotl(
      const vector_type x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }

  void calc() noexcept {
    for (uint64_t i = 0; i < VecResCount; i++) {
      auto& curState = state[i];
      vecRes[i] = rotl(curState[0] + curState[3], 23) + curState[0];
      const auto t = curState[1] << 17;
      curState[2] ^= curState[0];
      curState[3] ^= curState[1];
      curState[1] ^= curState[2];
      curState[0] ^= curState[3];
      curState[2] ^= t;
      curState[3] = rotl(curState[3], 45);
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

using xoshiro256pp_32 = xoshiro256pp<uint32_t>;
using xoshiro256pp_64 = xoshiro256pp<uint64_t>;

} // namespace folly
