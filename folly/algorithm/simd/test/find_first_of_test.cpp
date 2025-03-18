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

#include <folly/algorithm/simd/find_first_of.h>
#include <folly/algorithm/simd/find_first_of_extra.h>

#include <algorithm>
#include <random>
#include <string_view>

#include <folly/String.h>
#include <folly/portability/GTest.h>

using namespace std::literals;

static auto quote(std::string_view sv) {
  return "\"" + folly::cEscape<std::string>(sv) + "\"";
}

template <typename Param>
struct FindFirstOpOfTest : testing::TestWithParam<Param> {
  using finder_type = Param;

  static inline constexpr size_t big = 40;

  static constexpr finder_type make_finder(std::string_view const alphabet) {
    return finder_type(alphabet);
  }

  std::mt19937 rng;

  auto make_from(std::string_view const in, size_t const len) {
    std::string s;
    for (size_t i = 0; i < len; ++i) {
      s += in[rng() % in.size()];
    }
    return s;
  }
};

template <typename Param>
struct FindFirstOfTest : FindFirstOpOfTest<Param> {
  static inline constexpr auto json_strbound = "\"\\"sv;
  static inline constexpr auto json_loalpha = "abcdefghijklmnopqrstuvwxyz"sv;
};

TYPED_TEST_SUITE_P(FindFirstOfTest);

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = self::make_from(self::json_loalpha, i);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s = fmt::format(R"({}\{})", loa, loa);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc2) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s = fmt::format(R"({}\\{})", loa, loa);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc2n) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s = fmt::format(
        R"({}\{}\{})", loa, self::make_from(self::json_loalpha, 1), loa);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc_edge) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s = fmt::format(R"({}\)", loa);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc2_edge) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s = fmt::format(R"({}\\)", loa);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstOfTest, json_str_stress_loalpha_with_esc2n_edge) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_strbound);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const loa = self::make_from(self::json_loalpha, i);
    auto const s =
        fmt::format(R"({}\{}\)", loa, self::make_from(self::json_loalpha, 1));
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

// clang-format off
REGISTER_TYPED_TEST_SUITE_P(
    FindFirstOfTest
    , json_str_stress_loalpha
    , json_str_stress_loalpha_with_esc
    , json_str_stress_loalpha_with_esc2
    , json_str_stress_loalpha_with_esc2n
    , json_str_stress_loalpha_with_esc_edge
    , json_str_stress_loalpha_with_esc2_edge
    , json_str_stress_loalpha_with_esc2n_edge
    );
// clang-format on

struct find_first_of_impls {
  using stdfind_scalar = folly::simd::stdfind_scalar_finder_first_of;
  using default_scalar = folly::simd::default_scalar_finder_first_of;
  using ltindex_scalar = folly::simd::ltindex_scalar_finder_first_of;
  using ltsparse_scalar = folly::simd::ltsparse_scalar_finder_first_of;
#if __cpp_lib_execution >= 201902L
  using stdfind_vector = folly::simd::stdfind_vector_finder_first_of;
#endif
  using rngfind_vector = folly::simd::rngfind_vector_finder_first_of;
  using default_vector = folly::simd::composite_finder_first_of<
      folly::simd::default_vector_finder_first_of,
      default_scalar>;
  using shuffle_vector = folly::simd::composite_finder_first_of<
      folly::simd::shuffle_vector_finder_first_of,
      default_scalar>;
  using azmatch_vector = folly::simd::composite_finder_first_of<
      folly::simd::azmatch_vector_finder_first_of,
      default_scalar>;
};

INSTANTIATE_TYPED_TEST_SUITE_P(
    stdfind_scalar, FindFirstOfTest, find_first_of_impls::stdfind_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    default_scalar, FindFirstOfTest, find_first_of_impls::default_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    ltindex_scalar, FindFirstOfTest, find_first_of_impls::ltindex_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    ltsparse_scalar, FindFirstOfTest, find_first_of_impls::ltsparse_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    rngfind_vector, FindFirstOfTest, find_first_of_impls::rngfind_vector);
INSTANTIATE_TYPED_TEST_SUITE_P(
    default_vector, FindFirstOfTest, find_first_of_impls::default_vector);
INSTANTIATE_TYPED_TEST_SUITE_P(
    shuffle_vector, FindFirstOfTest, find_first_of_impls::shuffle_vector);
INSTANTIATE_TYPED_TEST_SUITE_P(
    azmatch_vector, FindFirstOfTest, find_first_of_impls::azmatch_vector);

template <typename Param>
struct FindFirstNotOfTest : FindFirstOpOfTest<Param> {
  static inline constexpr auto json_whitespace = " \r\n\t"sv;

  static inline constexpr auto json_blacktext = "[[[blackspace]]]"sv;
  static inline constexpr auto json_nulltext = "\0   [blackspace]"sv;
  static inline constexpr auto json_hitext = "\xff   [blackspace]"sv;

  static inline constexpr auto loweq_chars = "\x47\x57"sv;

  static inline constexpr auto weird_chars = "\x47\x83"sv;

  auto make_json_spaces(size_t const len) { //
    return std::string(len, ' ');
  }
};

TYPED_TEST_SUITE_P(FindFirstNotOfTest);

// dup?
TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_whitespace) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = self::make_from(self::json_whitespace, i);
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_spaces) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_spaces_then_text) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_blacktext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_spaces_then_nulls) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_nulltext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_spaces_then_his) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_hitext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_wspaces) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_wspaces_then_text) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_blacktext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_wspaces_then_nulls) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_nulltext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, json_ws_stress_wspaces_then_his) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::json_whitespace);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_json_spaces(i) //
        + std::string(self::json_hitext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, loweq_stress) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::loweq_chars);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_from(self::loweq_chars, i) //
        + std::string(self::json_blacktext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

TYPED_TEST_P(FindFirstNotOfTest, weird_stress) {
  using self = folly::remove_cvref_t<decltype(*this)>;
  constexpr auto const finder = self::make_finder(self::weird_chars);

  for (size_t i = 0; i <= self::big; ++i) {
    auto const s = ""s //
        + self::make_from(self::weird_chars, i) //
        + std::string(self::json_blacktext) //
        ;
    EXPECT_EQ(i, finder(s)) //
        << "input: " << quote(s);
  }
}

// clang-format off
REGISTER_TYPED_TEST_SUITE_P(
    FindFirstNotOfTest
    , json_ws_stress_whitespace // dup?
    , json_ws_stress_spaces
    , json_ws_stress_spaces_then_text
    , json_ws_stress_spaces_then_nulls
    , json_ws_stress_spaces_then_his
    , json_ws_stress_wspaces
    , json_ws_stress_wspaces_then_text
    , json_ws_stress_wspaces_then_nulls
    , json_ws_stress_wspaces_then_his
    , loweq_stress
    , weird_stress
    );
// clang-format on

struct find_first_not_of_impls {
  using default_scalar = folly::simd::default_scalar_finder_first_not_of;
  using ltindex_scalar = folly::simd::ltindex_scalar_finder_first_not_of;
  using ltsparse_scalar = folly::simd::ltsparse_scalar_finder_first_not_of;
  using default_vector = folly::simd::composite_finder_first_of<
      folly::simd::default_vector_finder_first_not_of,
      default_scalar>;
  using shuffle_vector = folly::simd::composite_finder_first_of<
      folly::simd::shuffle_vector_finder_first_not_of,
      default_scalar>;
  using azmatch_vector = folly::simd::composite_finder_first_of<
      folly::simd::azmatch_vector_finder_first_not_of,
      default_scalar>;
};

INSTANTIATE_TYPED_TEST_SUITE_P(
    default_scalar,
    FindFirstNotOfTest,
    find_first_not_of_impls::default_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    ltindex_scalar,
    FindFirstNotOfTest,
    find_first_not_of_impls::ltindex_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    ltsparse_scalar,
    FindFirstNotOfTest,
    find_first_not_of_impls::ltsparse_scalar);
INSTANTIATE_TYPED_TEST_SUITE_P(
    default_vector,
    FindFirstNotOfTest,
    find_first_not_of_impls::default_vector);
INSTANTIATE_TYPED_TEST_SUITE_P(
    shuffle_vector,
    FindFirstNotOfTest,
    find_first_not_of_impls::shuffle_vector);
INSTANTIATE_TYPED_TEST_SUITE_P(
    azmatch_vector,
    FindFirstNotOfTest,
    find_first_not_of_impls::azmatch_vector);
