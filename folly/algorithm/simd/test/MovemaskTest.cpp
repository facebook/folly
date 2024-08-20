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

#include <folly/algorithm/simd/Movemask.h>

#include <folly/Portability.h>
#include <folly/portability/GTest.h>

#include <array>
#include <cstdint>
#include <cstring>

#if FOLLY_X64
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

template <typename Reg, typename T, std::size_t N>
Reg loadReg(const std::array<T, N>& arr) {
  Reg res;
  std::memcpy(&res, arr.data(), sizeof(T) * N);
  return res;
}

std::uint64_t safeShift(std::uint64_t what, std::uint32_t shift) {
  if (!shift) {
    return what;
  }
  what <<= shift - 1;
  what <<= 1;
  return what;
}

template <typename Reg, typename T, std::size_t N>
void allOneTrueTests() {
  constexpr auto kTrue = static_cast<T>(-1);
  constexpr auto kFalse = static_cast<T>(0);

  std::array<T, N> arr;
  arr.fill(kFalse);

  ASSERT_EQ(0, folly::movemask<T>(loadReg<Reg>(arr)).first);

  for (std::size_t i = 0; i != N; ++i) {
    arr[i] = kTrue;
    auto [bits, bitsPerElement] = folly::movemask<T>(loadReg<Reg>(arr));
    std::uint64_t oneElement = safeShift(1, bitsPerElement()) - 1;
    std::uint64_t expectedBits = safeShift(oneElement, i * bitsPerElement());

    ASSERT_EQ(expectedBits, bits) << "sizeof(T): " << sizeof(T) << " i: " << i;
    arr[i] = kFalse;
  }
}

#if FOLLY_X64

TEST(Movemask, Sse2) {
  allOneTrueTests<__m128i, std::uint8_t, 16>();
  allOneTrueTests<__m128i, std::uint16_t, 8>();
  allOneTrueTests<__m128i, std::uint32_t, 4>();
  allOneTrueTests<__m128i, std::uint64_t, 2>();
}

#if defined(__AVX2__)

TEST(Movemask, Avx2) {
  allOneTrueTests<__m256i, std::uint8_t, 32>();
  allOneTrueTests<__m256i, std::uint16_t, 16>();
  allOneTrueTests<__m256i, std::uint32_t, 8>();
  allOneTrueTests<__m256i, std::uint64_t, 4>();
}

#endif

#endif

#if FOLLY_AARCH64

TEST(Movemask, AARCH64) {
  allOneTrueTests<uint8x8_t, std::uint8_t, 8>();
  allOneTrueTests<uint16x4_t, std::uint16_t, 4>();
  allOneTrueTests<uint32x2_t, std::uint32_t, 2>();
  allOneTrueTests<uint64x1_t, std::uint64_t, 1>();

  allOneTrueTests<uint8x16_t, std::uint8_t, 16>();
  allOneTrueTests<uint16x8_t, std::uint16_t, 8>();
  allOneTrueTests<uint32x4_t, std::uint32_t, 4>();
  allOneTrueTests<uint64x2_t, std::uint64_t, 2>();
}

#endif
