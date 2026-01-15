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

#include <folly/result/rich_error_code.h>

#include <memory>
#include <optional>
#include <typeinfo>

#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/portability/GTest.h>

#include <folly/result/immortal_rich_error.h>
#include <folly/result/rich_error.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

namespace folly {

constexpr bool testPlain() {
  constexpr auto ec_ptr = immortal_rich_error<
      ErrorC,
      A1::ONE_A1,
      B1{ImplB1::TWO_B1},
      B2::ONE_B2,
      C1::TWO_C1>.ptr();
  auto& ec = *get_rich_error(ec_ptr);

  test(get_rich_error_code<A1>(ec) == A1::ONE_A1);
  test(get_rich_error_code<B1>(ec) == B1{ImplB1::TWO_B1});
  test(get_rich_error_code<B2>(ec) == B2::ONE_B2);
  test(get_rich_error_code<C1>(ec) == C1::TWO_C1);
  return true;
}
static_assert(testPlain());

constexpr bool testPolymorphic() {
  constexpr auto ea_ptr = immortal_rich_error<ErrorA, A1::ZERO_A1>.ptr();
  auto& ea = *get_rich_error(ea_ptr);
  test(get_rich_error_code<A1>(ea) == A1::ZERO_A1);
  test(get_rich_error_code<B1>(ea) == std::nullopt);
  test(get_rich_error_code<B2>(ea) == std::nullopt);
  test(get_rich_error_code<C1>(ea) == std::nullopt);

  constexpr auto eb_ptr =
      immortal_rich_error<ErrorB, A1::ONE_A1, B1{ImplB1::ONE_B1}, B2::TWO_B2>.ptr();
  auto& eb = *get_rich_error(eb_ptr);
  test(get_rich_error_code<A1>(eb) == A1::ONE_A1);
  test(get_rich_error_code<B1>(eb) == B1{ImplB1::ONE_B1});
  test(get_rich_error_code<B2>(eb) == B2::TWO_B2);
  test(get_rich_error_code<C1>(eb) == std::nullopt);

  constexpr auto ec_ptr = immortal_rich_error<
      ErrorC,
      A1::TWO_A1,
      B1{ImplB1::ZERO_B1},
      B2::ZERO_B2,
      C1::ONE_C1>.ptr();
  auto& ec = *get_rich_error(ec_ptr);
  test(get_rich_error_code<A1>(ec) == A1::TWO_A1);
  test(get_rich_error_code<B1>(ec) == B1{ImplB1::ZERO_B1});
  test(get_rich_error_code<B2>(ec) == B2::ZERO_B2);
  test(get_rich_error_code<C1>(ec) == C1::ONE_C1);

  return true;
}
static_assert(testPolymorphic());

TEST(RichErrorCodeTest, Plain) {
  rich_error<ErrorC> ec(A1::ONE_A1, B1{ImplB1::TWO_B1}, B2::ONE_B2, C1::TWO_C1);

  EXPECT_EQ(get_rich_error_code<A1>(ec), A1::ONE_A1);
  EXPECT_EQ(get_rich_error_code<B1>(ec), B1{ImplB1::TWO_B1});
  EXPECT_EQ(get_rich_error_code<B2>(ec), B2::ONE_B2);
  EXPECT_EQ(get_rich_error_code<C1>(ec), C1::TWO_C1);
}

TEST(RichErrorCodeTest, Polymorphic) {
  {
    std::unique_ptr<rich_error_base> err =
        std::make_unique<rich_error<ErrorA>>(A1::ZERO_A1);
    EXPECT_EQ(get_rich_error_code<A1>(*err), A1::ZERO_A1);
    EXPECT_EQ(get_rich_error_code<B1>(*err), std::nullopt);
    EXPECT_EQ(get_rich_error_code<B2>(*err), std::nullopt);
    EXPECT_EQ(get_rich_error_code<C1>(*err), std::nullopt);
  }
  {
    std::unique_ptr<rich_error_base> err = std::make_unique<rich_error<ErrorB>>(
        A1::ONE_A1, B1{ImplB1::ONE_B1}, B2::TWO_B2);
    EXPECT_EQ(get_rich_error_code<A1>(*err), A1::ONE_A1);
    EXPECT_EQ(get_rich_error_code<B1>(*err), B1{ImplB1::ONE_B1});
    EXPECT_EQ(get_rich_error_code<B2>(*err), B2::TWO_B2);
    EXPECT_EQ(get_rich_error_code<C1>(*err), std::nullopt);
  }
  {
    std::unique_ptr<rich_error_base> err = std::make_unique<rich_error<ErrorC>>(
        A1::TWO_A1, B1{ImplB1::ZERO_B1}, B2::ZERO_B2, C1::ONE_C1);
    EXPECT_EQ(get_rich_error_code<A1>(*err), A1::TWO_A1);
    EXPECT_EQ(get_rich_error_code<B1>(*err), B1{ImplB1::ZERO_B1});
    EXPECT_EQ(get_rich_error_code<B2>(*err), B2::ZERO_B2);
    EXPECT_EQ(get_rich_error_code<C1>(*err), C1::ONE_C1);
  }
}

TEST(RichErrorCodeTest, Fmt) {
  EXPECT_EQ(
      fmt::format("{}", rich_error<ErrorA>(A1::ZERO_A1).all_codes_for_fmt()),
      "A1=0");
  EXPECT_EQ(
      fmt::format(
          "{}",
          rich_error<ErrorB>(A1::ONE_A1, B1{ImplB1::ONE_B1}, B2::TWO_B2)
              .all_codes_for_fmt()),
      "B1=11, B2=-22, A1=1");
  EXPECT_EQ(
      fmt::format(
          "{}",
          rich_error<ErrorC>(
              A1::TWO_A1, B1{ImplB1::ZERO_B1}, B2::ZERO_B2, C1::ONE_C1)
              .all_codes_for_fmt()),
      // NB: The name for C1 is automatic via `pretty_name<C1>()`.
      fmt::format("folly::C1=101, B1=10, B2=-20, A1=2"));
}

enum class BadHasReservedUuid { V };

template <> // NOLINT(facebook-hte-MisplacedTemplateSpecialization)
struct rich_error_code<BadHasReservedUuid> {
  static constexpr uint64_t uuid = 100; // BUG: 0 - 100,000 is reserved
};

struct ReservedUuidErr : rich_error_base {
  constexpr BadHasReservedUuid code() const { return BadHasReservedUuid::V; }
  using folly_rich_error_codes_t [[maybe_unused]] =
      rich_error_bases_and_own_codes<
          ReservedUuidErr,
          tag_t<>,
          &ReservedUuidErr::code>;
  constexpr void retrieve_code(
      [[maybe_unused]] rich_error_code_query& q) const override {
#if 0 // Manual test
      folly_rich_error_codes_t::retrieve_code(*this, q);
#endif
  }
  using folly_get_exception_hint_types = rich_error_hints<ReservedUuidErr>;
};

enum class BadHasDuplicateUuid { V };

template <> // NOLINT(facebook-hte-MisplacedTemplateSpecialization)
struct rich_error_code<BadHasDuplicateUuid> {
  static constexpr uint64_t uuid = rich_error_code<A1>::uuid; // BUG: duplicate
};

struct DuplicateUuidErr : ErrorA {
  constexpr BadHasDuplicateUuid code() const { return BadHasDuplicateUuid::V; }
  using folly_rich_error_codes_t [[maybe_unused]] =
      rich_error_bases_and_own_codes<
          DuplicateUuidErr,
          tag_t<ErrorA>,
          &DuplicateUuidErr::code>;
  constexpr void retrieve_code(
      [[maybe_unused]] rich_error_code_query& q) const override {
#if 0 // Manual test
      folly_rich_error_codes_t::retrieve_code(*this, q);
#endif
  }
};

} // namespace folly
