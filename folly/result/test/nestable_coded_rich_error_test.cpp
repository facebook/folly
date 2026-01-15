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

#include <folly/result/nestable_coded_rich_error.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

#if FOLLY_HAS_RESULT

namespace folly {

using namespace folly::string_literals;

const auto test_file_name = source_location::current().file_name();

void checkConstructionAndAccess(
    auto next, const char* next_what, std::string next_re) {
  auto err = make_nestable_coded_rich_error(
      A1::TWO_A1, rich_exception_ptr{std::move(next)}, "outer");

  EXPECT_EQ(err.code(), A1::TWO_A1);
  EXPECT_EQ(get_rich_error_code<A1>(err), A1::TWO_A1);
  EXPECT_STREQ(err.partial_message(), "outer");
  EXPECT_STREQ(
      next_what,
      get_exception<decltype(next)>(*err.next_error_for_enriched_message())
          ->what());

  checkFormatOfErrAndRep<
      nestable_coded_rich_error<A1>,
      rich_error_base,
      std::exception>(
      err,
      fmt::format(
          "outer - A1=2 @ {}:[0-9]+ \\[after\\] {}", test_file_name, next_re));
}

TEST(NestableCodedRichErrorTest, constructAndAccessWithNonRichNext) {
  checkConstructionAndAccess(
      std::logic_error{"inner"}, "inner", "std::logic_error: inner");
}

TEST(NestableCodedRichErrorTest, constructAndAccessWithRichNext) {
  int err_line = source_location::current().line() + 1;
  auto err = make_coded_rich_error(C1::TWO_C1);
  checkConstructionAndAccess(
      std::move(err),
      pretty_name<decltype(err)>(), // `what()` for the empty-message case
      fmt::format("folly::C1=102 @ {}:{}", test_file_name, err_line));
}

TEST(NestableCodedRichErrorTest, nestThreeLevels) {
  rich_exception_ptr base{make_coded_rich_error(A1::ONE_A1, "base")};

  rich_exception_ptr middle{
      make_nestable_coded_rich_error(B2::ZERO_B2, base, "middle")};
  EXPECT_EQ(get_rich_error_code<A1>(middle), std::nullopt); // NOT inherited!
  EXPECT_EQ(get_rich_error_code<B2>(middle), B2::ZERO_B2);

  auto top = make_nestable_coded_rich_error(A1::TWO_A1, middle, "top");
  EXPECT_EQ(top.code(), A1::TWO_A1);
  EXPECT_EQ(get_rich_error_code<A1>(top), A1::TWO_A1);
  EXPECT_EQ(get_rich_error_code<B2>(top), std::nullopt); // NOT inherited!

  checkFormatOfErrAndRep<
      nestable_coded_rich_error<A1>,
      rich_error_base,
      std::exception>(
      top,
      fmt::format(
          "top - A1=2 @ {}:[0-9]+ \\[after\\] middle - B2=-20 @ {}:[0-9]+ "
          "\\[after\\] base - A1=1 @ {}:[0-9]+",
          test_file_name,
          test_file_name,
          test_file_name));
}

TEST(NestableCodedRichErrorTest, multipleCodes) {
  rich_exception_ptr inner{make_coded_rich_error(A1::ONE_A1, "inner")};
  auto err = make_nestable_coded_rich_error(
      rich_msg{"multi"}, inner, A1::ONE_A1, B2::TWO_B2);

  EXPECT_EQ(get_rich_error_code<A1>(err), A1::ONE_A1);
  EXPECT_EQ(get_rich_error_code<B2>(err), B2::TWO_B2);
  EXPECT_EQ(err.template code_of_type<A1>(), A1::ONE_A1);
  EXPECT_EQ(err.template code_of_type<B2>(), B2::TWO_B2);
  EXPECT_STREQ(err.partial_message(), "multi");

  checkFormatOfErrAndRep<
      nestable_coded_rich_error<A1, B2>,
      rich_error_base,
      std::exception>(
      err,
      fmt::format(
          "multi - A1=1, B2=-22 @ {}:[0-9]+ \\[after\\] inner - A1=1 @ {}:[0-9]+",
          test_file_name,
          test_file_name));
}

TEST(NestableCodedRichErrorTest, inheritedCodes) {
  rich_exception_ptr inner{make_coded_rich_error(A1::ONE_A1, "inner")};
  auto err =
      make_inheriting_coded_rich_error(rich_msg{"outer"}, inner, B2::TWO_B2);

  EXPECT_EQ(get_rich_error_code<A1>(err), A1::ONE_A1);
  EXPECT_EQ(get_rich_error_code<B2>(err), B2::TWO_B2);
  EXPECT_EQ(err.code_of_type<B2>(), B2::TWO_B2);
  EXPECT_STREQ(err.partial_message(), "outer");

  checkFormatOfErrAndRep<
      inheriting_coded_rich_error<B2>,
      rich_error_base,
      std::exception>(
      err,
      fmt::format(
          "outer - B2=-22 @ {}:[0-9]+ \\[after\\] inner - A1=1 @ {}:[0-9]+",
          test_file_name,
          test_file_name));
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
