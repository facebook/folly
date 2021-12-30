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

#include <folly/lang/ToAscii.h>

#include <string>

#include <folly/portability/GTest.h>

template <typename F>
static void to_ascii_size_16(F _) {
  EXPECT_EQ(1, _(0x0));
  EXPECT_EQ(1, _(0x1));
  EXPECT_EQ(1, _(0xf));
  EXPECT_EQ(2, _(0x10));
  EXPECT_EQ(2, _(0x11));
  EXPECT_EQ(2, _(0xff));
  EXPECT_EQ(3, _(0x100));
  EXPECT_EQ(3, _(0x101));
  EXPECT_EQ(15, _(0xfffffffffffffff));
  EXPECT_EQ(16, _(0x1000000000000000));
  EXPECT_EQ(16, _(0x1000000000000001));
  EXPECT_EQ(16, _(0xffffffffffffffff));
}

template <typename F>
static void to_ascii_size_10(F _) {
  EXPECT_EQ(1, _(0));
  EXPECT_EQ(1, _(1));
  EXPECT_EQ(1, _(9));
  EXPECT_EQ(2, _(10));
  EXPECT_EQ(2, _(99));
  EXPECT_EQ(3, _(100));
  EXPECT_EQ(3, _(999));
  EXPECT_EQ(4, _(1000));
  EXPECT_EQ(4, _(9999));
  EXPECT_EQ(20, _(18446744073709551615u));
}

template <typename F>
static void to_ascii_size_8(F _) {
  EXPECT_EQ(1, _(00));
  EXPECT_EQ(1, _(01));
  EXPECT_EQ(1, _(07));
  EXPECT_EQ(2, _(010));
  EXPECT_EQ(2, _(011));
  EXPECT_EQ(2, _(077));
  EXPECT_EQ(3, _(0100));
  EXPECT_EQ(3, _(0101));
  EXPECT_EQ(21, _(0777777777777777777777));
  EXPECT_EQ(22, _(01000000000000000000000));
  EXPECT_EQ(22, _(01000000000000000000001));
  EXPECT_EQ(22, _(01777777777777777777777));
}

template <typename F>
static void to_ascii_size_u64_10_compare(F _) {
  //  the first X nonnegatives
  for (uint64_t i = 0; i < 100000; i++) {
    EXPECT_EQ(std::to_string(i).size(), _(i));
  }

  //  all powers of 2
  for (auto p = 0; p < 64; p++) {
    for (auto n : {-1, 0, 1}) {
      auto const v = folly::constexpr_pow(uint64_t(2), p) + n;
      EXPECT_EQ(std::to_string(v).size(), _(v));
    }
  }

  //  all powers of 10
  for (auto p = 0; p < 20; p++) {
    for (auto n : {-1, 0, 1}) {
      auto const v = folly::constexpr_pow(uint64_t(2), p) + n;
      EXPECT_EQ(std::to_string(v).size(), _(v));
    }
  }
}

struct ToAsciiTest : testing::Test {};

TEST_F(ToAsciiTest, to_ascii_powers_u64_16) {
  using _ = folly::detail::to_ascii_powers<16, uint64_t>;
  EXPECT_EQ(16, _::size);
  EXPECT_EQ(1, _::data.data[0]);
  EXPECT_EQ(16, _::data.data[1]);
  EXPECT_EQ(256, _::data.data[2]);
}

TEST_F(ToAsciiTest, to_ascii_powers_u64_10) {
  using _ = folly::detail::to_ascii_powers<10, uint64_t>;
  EXPECT_EQ(20, _::size);
  EXPECT_EQ(1, _::data.data[0]);
  EXPECT_EQ(10, _::data.data[1]);
  EXPECT_EQ(100, _::data.data[2]);
}

TEST_F(ToAsciiTest, to_ascii_powers_u64_8) {
  using _ = folly::detail::to_ascii_powers<8, uint64_t>;
  EXPECT_EQ(22, _::size);
  EXPECT_EQ(1, _::data.data[0]);
  EXPECT_EQ(8, _::data.data[1]);
  EXPECT_EQ(64, _::data.data[2]);
}

TEST_F(ToAsciiTest, to_ascii_size_max_u64_16) {
  constexpr auto const actual = folly::to_ascii_size_max<16, uint64_t>;
  EXPECT_EQ(16, actual);
}

TEST_F(ToAsciiTest, to_ascii_size_max_u64_10) {
  constexpr auto const actual = folly::to_ascii_size_max<10, uint64_t>;
  EXPECT_EQ(20, actual);
}

TEST_F(ToAsciiTest, to_ascii_size_max_u64_8) {
  constexpr auto const actual = folly::to_ascii_size_max<8, uint64_t>;
  EXPECT_EQ(22, actual);
}

TEST_F(ToAsciiTest, to_ascii_size_imuls_16) {
  to_ascii_size_16(folly::detail::to_ascii_size_imuls<16>);
}

TEST_F(ToAsciiTest, to_ascii_size_idivs_16) {
  to_ascii_size_16(folly::detail::to_ascii_size_idivs<16>);
}

TEST_F(ToAsciiTest, to_ascii_size_array_16) {
  to_ascii_size_16(folly::detail::to_ascii_size_array<16>);
}

TEST_F(ToAsciiTest, to_ascii_size_clzll_16) {
  to_ascii_size_16(folly::detail::to_ascii_size_clzll<16>);
}

TEST_F(ToAsciiTest, to_ascii_size_imuls_10) {
  to_ascii_size_10(folly::detail::to_ascii_size_imuls<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_idivs_10) {
  to_ascii_size_10(folly::detail::to_ascii_size_idivs<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_array_10) {
  to_ascii_size_10(folly::detail::to_ascii_size_array<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_clzll_10) {
  to_ascii_size_10(folly::detail::to_ascii_size_clzll<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_imuls_8) {
  to_ascii_size_8(folly::detail::to_ascii_size_imuls<8>);
}

TEST_F(ToAsciiTest, to_ascii_size_idivs_8) {
  to_ascii_size_8(folly::detail::to_ascii_size_idivs<8>);
}

TEST_F(ToAsciiTest, to_ascii_size_array_8) {
  to_ascii_size_8(folly::detail::to_ascii_size_array<8>);
}

TEST_F(ToAsciiTest, to_ascii_size_clzll_8) {
  to_ascii_size_8(folly::detail::to_ascii_size_clzll<8>);
}

TEST_F(ToAsciiTest, to_ascii_size_imuls_10_compare) {
  to_ascii_size_u64_10_compare(folly::detail::to_ascii_size_imuls<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_idivs_10_compare) {
  to_ascii_size_u64_10_compare(folly::detail::to_ascii_size_idivs<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_array_10_compare) {
  to_ascii_size_u64_10_compare(folly::detail::to_ascii_size_array<10>);
}

TEST_F(ToAsciiTest, to_ascii_size_clzll_10_compare) {
  to_ascii_size_u64_10_compare(folly::detail::to_ascii_size_clzll<10>);
}

template <uint64_t Base>
struct inputs;

template <>
struct inputs<10> {
  static constexpr uint64_t const data[22] = {
      0,
      1,
      12,
      123,
      1234,
      12345,
      123456,
      1234567,
      12345678,
      123456789,
      1234567890,
      12345678901,
      123456789012,
      1234567890123,
      12345678901234,
      123456789012345,
      1234567890123456,
      12345678901234567,
      123456789012345678,
      1234567890123456789,
      -2ull,
      -1ull,
  };
};
constexpr uint64_t const inputs<10>::data[22];

template <uint64_t Base, typename F>
static void to_ascii_compare(F _) {
  for (auto const n : inputs<Base>::data) {
    auto const expected = std::to_string(n);
    SCOPED_TRACE(expected);
    std::string actual(folly::to_ascii_size_max<Base, uint64_t>, '\0');
    auto const size = _(&actual[0], n);
    EXPECT_EQ(expected.size(), size);
    EXPECT_EQ(expected, actual.substr(0, size));
  }
}

using abc = folly::to_ascii_alphabet_lower;

TEST_F(ToAsciiTest, to_ascii_basic_10_compare) {
  to_ascii_compare<10>([](auto out, auto v) {
    auto size = folly::to_ascii_size<10>(v);
    folly::detail::to_ascii_with_basic<10, abc>(out, size, v);
    return size;
  });
}

TEST_F(ToAsciiTest, to_ascii_array_10_compare) {
  to_ascii_compare<10>([](auto out, auto v) {
    auto size = folly::to_ascii_size<10>(v);
    folly::detail::to_ascii_with_array<10, abc>(out, size, v);
    return size;
  });
}

TEST_F(ToAsciiTest, to_ascii_table_10_compare) {
  to_ascii_compare<10>([](auto out, auto v) {
    auto size = folly::to_ascii_size<10>(v);
    folly::detail::to_ascii_with_table<10, abc>(out, size, v);
    return size;
  });
}
