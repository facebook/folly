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

#include <folly/result/coded_rich_error.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/immortal_rich_error.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

#if FOLLY_HAS_RESULT

namespace folly {

using namespace folly::string_literals;

const auto test_file_name = source_location::current().file_name();

constexpr bool testImmortalCodedRichError() {
  { // default message is empty
    constexpr auto eptr =
        immortal_rich_error<coded_rich_error<A1>, A1::TWO_A1>.ptr();
    test(std::string_view{""} == get_rich_error(eptr)->partial_message());
  }
  { // non-default message
    constexpr auto eptr =
        immortal_rich_error<coded_rich_error<A1>, A1::ONE_A1, "msg"_litv>.ptr();
    test(get_rich_error_code<A1>(eptr) == A1::ONE_A1);

    auto& err = *get_rich_error(eptr);
    test(get_rich_error_code<A1>(err) == A1::ONE_A1);
    test(std::string_view{"msg"} == err.partial_message());
  }
  return true;
}
static_assert(testImmortalCodedRichError());

constexpr bool testMultipleCodes() {
  constexpr auto eptr = immortal_rich_error<
        coded_rich_error<A1, B2>,
        A1::ONE_A1,
        B2::TWO_B2,
        "multi-code"_litv>.ptr();

  test(get_rich_error_code<A1>(eptr) == A1::ONE_A1);
  test(get_rich_error_code<B2>(eptr) == B2::TWO_B2);

  auto derived = get_exception<coded_rich_error<A1, B2>>(eptr);
  test(derived->code_of_type<A1>() == A1::ONE_A1);
  test(derived->code_of_type<B2>() == B2::TWO_B2);

  test(std::string_view{"multi-code"} == derived->partial_message());
  return true;
}
static_assert(testMultipleCodes());

TEST(CodedRichErrorTest, basics) {
  auto err_line = source_location::current().line() + 1;
  auto err1 = make_coded_rich_error(A1::ONE_A1, "msg");

  auto accessError = [&err_line](const auto& err) {
    EXPECT_EQ(err.code(), A1::ONE_A1);
    EXPECT_EQ(get_rich_error_code<A1>(err), A1::ONE_A1);
    EXPECT_STREQ(err.partial_message(), "msg");
    EXPECT_EQ(err_line, err.source_location().line());
    EXPECT_EQ(err.next_error_for_enriched_message(), nullptr);
  };

  accessError(err1);
  { // Copy ctor
    auto err2{err1}; // NOLINT(performance-unnecessary-copy-initialization)
    accessError(err2);
  }
  { // Move ctor
    auto err2{std::move(err1)};
    accessError(err2);
  }
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_STREQ(err1.partial_message(), ""); // moved out
}

TEST(CodedRichErrorTest, accessAndFormatError) {
  {
    auto err_line = source_location::current().line() + 1;
    auto err = make_coded_rich_error(A1::ONE_A1, "msg {} and {}", 42, "test");

    const char* msg = "msg 42 and test";
    EXPECT_STREQ(msg, err.partial_message());
    EXPECT_EQ(err.code(), A1::ONE_A1);
    EXPECT_EQ(err_line, err.source_location().line());

    checkFormatOfErrAndRep<
        coded_rich_error<A1>,
        rich_error_base,
        std::exception>(
        err, fmt::format("{} - A1=1 @ {}:{}", msg, test_file_name, err_line));
  }
  { // Also check formatting with an empty message
    auto err = make_coded_rich_error(A1::TWO_A1);
    checkFormatOfErrAndRep<
        coded_rich_error<A1>,
        rich_error_base,
        std::exception>(err, fmt::format("A1=2 @ {}:[0-9]+", test_file_name));
  }
}

TEST(CodedRichErrorTest, viaRichExceptionPtr) {
  rich_exception_ptr rep{make_coded_rich_error(A1::TWO_A1, "test")};

  EXPECT_EQ(get_rich_error_code<A1>(rep), A1::TWO_A1);

  auto* err = rep.get_outer_exception<coded_rich_error<A1>>();
  ASSERT_NE(err, nullptr);
  EXPECT_EQ(err->code(), A1::TWO_A1);
  EXPECT_STREQ(err->partial_message(), "test");
}

// Extend `coded_rich_error` to store additional data
TEST(CodedRichErrorTest, multipleCodes) {
  auto check_err = [](const auto& err) {
    EXPECT_EQ(get_rich_error_code<A1>(err), A1::ONE_A1);
    EXPECT_EQ(get_rich_error_code<B2>(err), B2::TWO_B2);
    EXPECT_EQ(err.template code_of_type<A1>(), A1::ONE_A1);
    EXPECT_EQ(err.template code_of_type<B2>(), B2::TWO_B2);
    EXPECT_STREQ(err.partial_message(), "multi");
  };
  // Test both multi-code factory functions
  check_err(make_coded_rich_error(rich_msg{"multi"}, A1::ONE_A1, B2::TWO_B2));
  check_err(coded_rich_error<A1, B2>::make(A1::ONE_A1, B2::TWO_B2, "multi"));
}

TEST(CodedRichErrorTest, customCodedRichError) {
  struct custom_coded_error : coded_rich_error<A1> {
    int n_;
    // NB: Taking `rich_msg` is the *simple* pattern for derived classes to be
    // able to auto-capture source location. But, you can also provide
    // a more sleek overload using `ext::format_string_and_location`,
    // like `coded_rich_error` does.
    static rich_error<custom_coded_error> make(A1 a1, int n, rich_msg msg) {
      return rich_error<custom_coded_error>{a1, n, std::move(msg)};
    }
    using folly_get_exception_hint_types = rich_error_hints<custom_coded_error>;

   protected:
    explicit custom_coded_error(A1 a1, int n, rich_msg msg)
        : coded_rich_error<A1>{a1, std::move(msg)}, n_{n} {}
  };

  auto err = custom_coded_error::make(A1::ONE_A1, 42, rich_msg{"hi"});
  EXPECT_EQ(err.code(), A1::ONE_A1);
  EXPECT_EQ(get_rich_error_code<A1>(err), A1::ONE_A1);
  EXPECT_EQ(err.n_, 42);
  EXPECT_STREQ(err.partial_message(), "hi");
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
