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

#include <cstring>
#include <random>
#include <string>
#include <utility>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>

template <int Base, typename F>
static void to_ascii_size_hammer(F _) {
  for (int sz = 1, b = 1, e = Base; sz <= 4; ++sz, b = e, e *= Base) {
    for (int i = b; i < e; ++i) {
      EXPECT_EQ(sz, _(i)) << i;
    }
  }
}

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

  to_ascii_size_hammer<16>(_);
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

  to_ascii_size_hammer<10>(_);
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

  to_ascii_size_hammer<8>(_);
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

//  The values at which the digit count changes: every power of two and every
//  power of Base, each with its neighbors. A change-of-base estimate that lands
//  on the wrong side of one of these is exactly how a sizing implementation
//  goes wrong, so these probes localize the defect rather than merely find it.
//
//  Use to_ascii_size_idivs as the oracle for the two helpers below. Repeated
//  division is the definition of the base-Base representation, so it shares no
//  structure with the log change-of-base estimate under test, and the tests
//  above already pin it against std::to_string and against known digit counts.
template <uint64_t Base, typename F>
static void to_ascii_size_boundaries(F _) {
  auto const check = [&](uint64_t v) {
    EXPECT_EQ(folly::detail::to_ascii_size_idivs<Base>(v), _(v))
        << "base " << Base << " v " << v;
  };
  auto const around = [&](uint64_t v) {
    if (v > 0) {
      check(v - 1);
    }
    check(v);
    if (v < ~uint64_t(0)) {
      check(v + 1);
    }
  };

  for (size_t p = 0; p < 64; ++p) {
    around(uint64_t(1) << p);
  }
  for (uint64_t p = 1; p <= (~uint64_t(0)) / Base; p *= Base) {
    around(p);
  }
  check(0);
  check(~uint64_t(0));
}

//  Every small value, then a spread of values of every bit length, since the
//  change-of-base estimate is driven by bit length.
template <uint64_t Base, typename F>
static void to_ascii_size_sweep(F _) {
  auto const check = [&](uint64_t v) {
    EXPECT_EQ(folly::detail::to_ascii_size_idivs<Base>(v), _(v))
        << "base " << Base << " v " << v;
  };

  for (uint64_t v = 0; v < 4096; ++v) {
    check(v);
  }

  std::mt19937_64 rng;
  for (size_t bits = 1; bits <= 64; ++bits) {
    auto const mask = bits == 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
    for (size_t i = 0; i < 64; ++i) {
      check((rng() & mask) | (uint64_t(1) << (bits - 1)));
    }
  }
}

template <auto... Bases>
static void to_ascii_size_clzll_bases(folly::vtag_t<Bases...>) {
  (to_ascii_size_boundaries<Bases>(folly::detail::to_ascii_size_clzll<Bases>),
   ...);
  (to_ascii_size_sweep<Bases>(folly::detail::to_ascii_size_clzll<Bases>), ...);
}

template <uint64_t Lo, uint64_t... I>
static folly::vtag_t<(Lo + I)...> to_ascii_bases_shifted_(
    std::integer_sequence<uint64_t, I...>);

//  The bases Lo through Hi inclusive, as a value list.
template <uint64_t Lo, uint64_t Hi>
using to_ascii_bases_range_ = decltype(to_ascii_bases_shifted_<Lo>(
    std::make_integer_sequence<uint64_t, Hi - Lo + 1>{}));

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

//  to_ascii_size_route sends only power-of-two bases and base 10 to the clzll
//  implementation, so the bases below are reachable only by calling it
//  directly. They are covered because the restriction lives in the caller:
//  nothing about to_ascii_size_clzll itself is specific to the bases the router
//  selects.
TEST_F(ToAsciiTest, to_ascii_size_clzll_every_base) {
  //  every base to_ascii_alphabet supports, then a few past it, where the
  //  change-of-base derivation answers to arithmetic and to no alphabet
  using bases_range = to_ascii_bases_range_<2, 36>;
  using bases_extra =
      folly::vtag_t<uint64_t(37), uint64_t(42), uint64_t(54), uint64_t(100)>;
  using bases =
      folly::value_list_concat_t<folly::vtag_t, bases_range, bases_extra>;

  //  a range computed wrong would still be a valid list of bases, and would
  //  still pass, so pin what it computed
  static_assert(
      std::is_same_v<folly::value_list_element_type_t<0, bases>, uint64_t>);
  static_assert(folly::value_list_size_v<bases> == 35 + 4);
  static_assert(folly::value_list_element_v<0, bases> == 2);
  static_assert(folly::value_list_element_v<34, bases> == 36);
  static_assert(folly::value_list_element_v<35, bases> == 37);
  static_assert(folly::value_list_element_v<38, bases> == 100);

  to_ascii_size_clzll_bases(bases{});
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

//  Both public entry points route through the same implementation choice, so
//  they agree byte for byte. They would still agree on a non-mobile build if
//  the array overload bypassed the route, so this pins the contract rather than
//  the routing itself.
TEST_F(ToAsciiTest, to_ascii_with_overloads_agree_10) {
  for (auto const v : inputs<10>::data) {
    SCOPED_TRACE(v);

    char arr[folly::to_ascii_size_max<10, uint64_t>];
    auto const arr_size = folly::to_ascii_decimal(arr, v);

    char rng[folly::to_ascii_size_max<10, uint64_t>];
    auto const rng_size = folly::to_ascii_decimal(rng, rng + sizeof(rng), v);

    EXPECT_EQ(std::to_string(v), std::string(arr, arr_size));
    EXPECT_EQ(std::string(arr, arr_size), std::string(rng, rng_size));
  }
}

//  The range overload reports a short buffer by writing nothing and returning
//  0; a successful call always writes at least one byte.
TEST_F(ToAsciiTest, to_ascii_with_range_too_small) {
  constexpr char const sentinel = '\x7f';
  char buf[8];
  std::memset(buf, sentinel, sizeof(buf));

  //  1234567890 needs 10 bytes and the range holds 8
  EXPECT_EQ(0, folly::to_ascii_decimal(buf, buf + sizeof(buf), 1234567890u));
  EXPECT_EQ(std::string(sizeof(buf), sentinel), std::string(buf, sizeof(buf)));

  //  an empty range is too small even for a single digit
  EXPECT_EQ(0, folly::to_ascii_decimal(buf, buf, 1u));
  EXPECT_EQ(std::string(sizeof(buf), sentinel), std::string(buf, sizeof(buf)));

  //  an exactly-sized range succeeds
  EXPECT_EQ(3, folly::to_ascii_decimal(buf, buf + 3, 123u));
  EXPECT_EQ("123", std::string(buf, 3));
}
