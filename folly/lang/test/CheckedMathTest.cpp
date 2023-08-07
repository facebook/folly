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

#include <folly/lang/CheckedMath.h>

#include <cstdint>
#include <limits>
#include <random>

#include <folly/portability/GTest.h>

TEST(CheckedMath, checked_add_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_add(&a, 5u, 4u));
  EXPECT_EQ(a, 9);
}

TEST(CheckedMath, checked_add_overflow) {
  unsigned int a;

  EXPECT_FALSE(
      folly::checked_add(&a, std::numeric_limits<unsigned int>::max(), 4u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_uint64_t_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, std::numeric_limits<uint64_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_3_no_overflow) {
  uint64_t a;

  EXPECT_TRUE(folly::checked_add<uint64_t>(&a, 5, 7, 9));
  EXPECT_EQ(a, 21);
}

TEST(CheckedMath, checked_add_3_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, 5, std::numeric_limits<uint64_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_3_overflow2) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, 5, 7, std::numeric_limits<uint64_t>::max() - 7));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_4_no_overflow) {
  uint64_t a;

  EXPECT_TRUE(folly::checked_add<uint64_t>(&a, 5, 7, 9, 11));
  EXPECT_EQ(a, 32);
}

TEST(CheckedMath, checked_add_4_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, 5, std::numeric_limits<uint64_t>::max() - 7, 9, 11));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_4_overflow2) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, 5, 7, std::numeric_limits<uint64_t>::max() - 7, 11));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_4_overflow3) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_add<uint64_t>(
      &a, 5, 7, 9, std::numeric_limits<uint64_t>::max() - 7));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_add_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::detail::generic_checked_add(&a, 5u, 4u));
  EXPECT_EQ(a, 9);
}

TEST(CheckedMath, generic_checked_add_overflow) {
  unsigned int a;

  EXPECT_FALSE(folly::detail::generic_checked_add(
      &a, std::numeric_limits<unsigned int>::max(), 4u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_add_uint64_t_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::detail::generic_checked_add<uint64_t>(
      &a, std::numeric_limits<uint64_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_div_safe_divisor) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_div(&a, 8u, 4u));
  EXPECT_EQ(a, 2);
}

TEST(CheckedMath, checked_div_zero_divisor) {
  unsigned int a;

  EXPECT_FALSE(folly::checked_div(&a, 8u, 0u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_mod_safe_divisor) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_mod(&a, 5u, 4u));
  EXPECT_EQ(a, 1);
}

TEST(CheckedMath, checked_mod_zero_divisor) {
  unsigned int a;

  EXPECT_FALSE(folly::checked_div(&a, 5u, 0u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_mul_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_mul(&a, 5u, 4u));
  EXPECT_EQ(a, 20);
}

TEST(CheckedMath, checked_mul_overflow) {
  unsigned int a;

  EXPECT_FALSE(
      folly::checked_mul(&a, std::numeric_limits<unsigned int>::max(), 4u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_mul_uint64_t_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::checked_mul<uint64_t>(
      &a, std::numeric_limits<uint64_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::detail::generic_checked_mul(&a, 5u, 4u));
  EXPECT_EQ(a, 20);
}

TEST(CheckedMath, generic_checked_mul_overflow) {
  unsigned int a;

  EXPECT_FALSE(folly::detail::generic_checked_mul(
      &a, std::numeric_limits<unsigned int>::max(), 4u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow) {
  uint64_t a;

  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, std::numeric_limits<uint64_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow_1) {
  uint64_t a;

  // lhs_high != 0 && rhs_high != 0
  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, 0x1'0000'0000, 0x1'0000'0000));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow_2) {
  uint64_t a;

  // lhs_low * rhs_high overflows
  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, 0xFFFF'FFFF, 0xF'0000'0000));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow_3) {
  uint64_t a;

  // lhs_high * rhs_low overflows
  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, 0xF'0000'0000, 0xFFFF'FFFF));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow_4) {
  uint64_t a;

  // mid_bits1 + mid_bits2 overflows
  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, 0x2'7FFF'FFFF, 0x2'7FFF'FFFF));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, generic_checked_mul_uint64_t_overflow_5) {
  uint64_t a;

  // (lhs_low * rhs_low) + (mid_bits << 32) overflows
  EXPECT_FALSE(folly::detail::generic_checked_mul<uint64_t>(
      &a, 0x2'0000'0203, 0x7FFF'FFFF));
  EXPECT_EQ(a, {});
}

// __builtin_mul_overflow has the most straight-forward interface for doing
// this check, and the generic code will be the same regardless of the compiler.
#if FOLLY_HAS_BUILTIN(__builtin_mul_overflow)
TEST(CheckedMath, generic_checked_mul_vs_builtin) {
  std::mt19937_64 mt{std::random_device()()};

  constexpr size_t IterationCount = 1'000'000;
  size_t overflowCount = 0;
  for (size_t i = 0; i < IterationCount; i++) {
    // There's probably a distribution that produces values on the
    // edge of likely overflow, but I don't know the name, so use
    // the full range for now and just drop bits from a and b to
    // get us down to only ~90% overflow, which is what we care the
    // most about testing.
    // 70 bits of random data across a & b nets 90% overflow
    // 69 bits of data nets 86% overflow
    // 68 bits of data nets 76% overflow
    uint64_t a = mt() >> 16;
    uint64_t b = mt() >> 43;
    uint64_t genRes = 0;
    uint64_t builtinRes = 0;
    bool genOverflow =
        folly::detail::generic_checked_mul<uint64_t>(&genRes, a, b);
    bool builtinOverflow = !__builtin_mul_overflow(a, b, &builtinRes);
    EXPECT_EQ(genOverflow, builtinOverflow);

    if (genOverflow && builtinOverflow) {
      // __builtin doesn't guarantee the value of builtinRes when an overflow
      // happens, but generic_checked_* guarantees it's zero.
      EXPECT_EQ(genRes, builtinRes);
    } else {
      overflowCount++;
    }
  }

  EXPECT_NE(overflowCount, IterationCount);
  EXPECT_NE(overflowCount, 0);
}
#endif

TEST(CheckedMath, checked_muladd_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_muladd(&a, 5u, 4u, 1u));
  EXPECT_EQ(a, 21);
}

TEST(CheckedMath, checked_muladd_overflow) {
  unsigned int a;

  EXPECT_FALSE(folly::checked_muladd(
      &a, 5u, std::numeric_limits<unsigned int>::max(), 1u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_muladd_overflow2) {
  unsigned int a;

  EXPECT_FALSE(folly::checked_muladd(
      &a, 5u, 4u, std::numeric_limits<unsigned int>::max()));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_ptr_add_no_overflow) {
  unsigned int buf[4];
  unsigned int* a;

  EXPECT_TRUE(folly::checked_add(&a, &buf[0], 1u));
  EXPECT_EQ(a, &buf[1]);
}

TEST(CheckedMath, checked_ptr_add_overflow) {
  unsigned int buf[4];
  unsigned int* a{nullptr};

  EXPECT_FALSE(folly::checked_add(
      &a, &buf[0], std::numeric_limits<uint64_t>::max() - 7));
  EXPECT_EQ(a, nullptr);
}
