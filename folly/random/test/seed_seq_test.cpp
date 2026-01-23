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

#include <folly/random/seed_seq.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <utility>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

extern "C" FOLLY_KEEP void check_folly_seed_seq_generate_match(
    folly::span<uint32_t, 1> dst, std::seed_seq& seq) {
  folly::seed_seq_generate(dst, seq);
}

struct SeedSeqTest : testing::Test {};

// Helper to extract the Nth byte from a word
template <std::size_t N>
struct at_byte_fn {
  template <typename T>
  uint8_t operator()(const T& word) const {
    static constexpr size_t size = sizeof(T);
    static_assert(N < size, "Byte index out of range");
    uint8_t bytes[size];
    std::memcpy(bytes, &word, size);
    return bytes[N];
  }
};

template <std::size_t N>
inline constexpr at_byte_fn<N> at_byte{};

// Test with same-sized types (uint32_t -> uint32_t)
TEST_F(SeedSeqTest, SameSizeTypes) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  std::array<uint32_t, 8> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
}

// Test with larger Word type (uint32_t result_type -> uint64_t Word)
TEST_F(SeedSeqTest, LargerWordType) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  std::array<uint64_t, 4> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<4>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<5>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<6>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<7>));
}

// Test with smaller Word type (uint32_t result_type -> uint16_t Word)
TEST_F(SeedSeqTest, SmallerWordType) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  std::array<uint16_t, 16> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
}

// Test with non-divisible sizes (uint32_t result_type -> uint64_t Word, odd
// count)
TEST_F(SeedSeqTest, NonDivisibleSize) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  // 3 uint64_t = 24 bytes, which is 6 uint32_t (evenly divisible)
  std::array<uint64_t, 3> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<4>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<5>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<6>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<7>));
}

// Test with truly non-divisible sizes (uint32_t result_type -> 3-byte struct)
TEST_F(SeedSeqTest, TrulyNonDivisibleSize) {
  struct ThreeByte {
    uint8_t bytes[3];
  };

  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  std::array<ThreeByte, 5> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), [](const auto& v) {
    return std::any_of(std::begin(v.bytes), std::end(v.bytes), std::identity{});
  }));
}

// Test with uint8_t Word type
TEST_F(SeedSeqTest, ByteWordType) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};
  std::seed_seq seq(seed_data.begin(), seed_data.end());

  std::array<uint8_t, 32> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), std::identity{}));
}

// Test determinism - same seed should produce same output
TEST_F(SeedSeqTest, Determinism) {
  std::array<uint32_t, 4> seed_data = {1, 2, 3, 4};

  std::array<uint64_t, 4> dst1{};
  std::seed_seq seq1(seed_data.begin(), seed_data.end());
  folly::seed_seq_generate(folly::span(dst1), seq1);

  std::array<uint64_t, 4> dst2{};
  std::seed_seq seq2(seed_data.begin(), seed_data.end());
  folly::seed_seq_generate(folly::span(dst2), seq2);

  EXPECT_EQ(dst1, dst2);
}

// Test that different seeds produce different outputs
TEST_F(SeedSeqTest, DifferentSeeds) {
  std::array<uint32_t, 4> seed_data1 = {1, 2, 3, 4};
  std::array<uint32_t, 4> seed_data2 = {5, 6, 7, 8};

  std::array<uint64_t, 4> dst1{};
  std::seed_seq seq1(seed_data1.begin(), seed_data1.end());
  folly::seed_seq_generate(folly::span(dst1), seq1);

  std::array<uint64_t, 4> dst2{};
  std::seed_seq seq2(seed_data2.begin(), seed_data2.end());
  folly::seed_seq_generate(folly::span(dst2), seq2);

  EXPECT_NE(dst1, dst2);
}

TEST_F(SeedSeqTest, MakeStdSeedSeqUint32) {
  using word_type = uint32_t;
  static_assert(sizeof(word_type) == sizeof(std::seed_seq::result_type));

  auto seq = folly::make_std_seed_seq(word_type(1));

  std::array<uint32_t, 4> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
}

TEST_F(SeedSeqTest, MakeStdSeedSeqUint64) {
  using word_type = uint64_t;
  static_assert(sizeof(word_type) > sizeof(std::seed_seq::result_type));

  auto seq = folly::make_std_seed_seq(word_type(1));

  std::array<uint64_t, 4> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<4>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<5>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<6>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<7>));
}

TEST_F(SeedSeqTest, MakeStdSeedSeqUint16) {
  using word_type = uint16_t;
  static_assert(sizeof(word_type) < sizeof(std::seed_seq::result_type));

  auto seq = folly::make_std_seed_seq(word_type(1));

  std::array<uint32_t, 4> dst{};
  folly::seed_seq_generate(folly::span(dst), seq);

  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<0>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<1>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<2>));
  EXPECT_TRUE(std::any_of(dst.begin(), dst.end(), at_byte<3>));
}

TEST_F(SeedSeqTest, MakeStdSeedSeqDeterminism) {
  uint64_t word = 0x123456789ABCDEF0;

  std::array<uint64_t, 4> dst1{};
  auto seq1 = folly::make_std_seed_seq(word);
  folly::seed_seq_generate(folly::span(dst1), seq1);

  std::array<uint64_t, 4> dst2{};
  auto seq2 = folly::make_std_seed_seq(word);
  folly::seed_seq_generate(folly::span(dst2), seq2);

  EXPECT_EQ(dst1, dst2);
}
