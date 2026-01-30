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

#include <folly/result/epitaph.h>

#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/coded_rich_error.h>

#if FOLLY_HAS_RESULT

namespace folly {

using namespace folly::string_literals;

const auto test_file_name = source_location::current().file_name();

struct TinyErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<TinyErr>;
};

// Minimal version of `accessEpitaphsAndUnderlyingException`, except we also
// check direct formatting of `detail::epitaph_non_value`.
TEST(RichErrorBaseFormatTest, SameErrorDirectThenUnderlying2) {
  struct MyErr : rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<MyErr>;
  };

  rich_exception_ptr inner{rich_error<MyErr>{}};
  auto err_line = source_location::current().line() + 1;
  rich_error<detail::epitaph_non_value> err{std::move(inner), rich_msg{"msg"}};
  checkFormatOfErrAndRep<MyErr, rich_error_base, std::exception>(
      err,
      fmt::format("MyErr \\[via\\] msg @ {}:{}", test_file_name, err_line));
}

// Wrap `TinyErr` with 2 levels of epitaph wrappers, and check the entire
// `rich_error_base` API (except codes).
//   - Epitaphs are transparent -- all interfaces (except
//     `get_outer_exception`) go to the underlying error.
//   - `get_exception<>()` returns a formattable pointer with epitaphs, and the
//     propagation history is visible in the message.
TEST(EpitaphTest, accessEpitaphsAndUnderlyingException) {
  std::string bare_msg = "folly::TinyErr";

  auto assertUnderlying = [&](auto& eos, const std::string& re) {
    {
      // Only `rich_exception_ptr` can show epitaphs AND expose the
      // underlying exception.  By converting to a bare `exception_ptr`, we
      // discard epitaphs.
      auto eptr = copy(eos).to_exception_ptr_slow();
      EXPECT_EQ(bare_msg, fmt::format("{}", *get_rich_error(eptr)));
      EXPECT_FALSE(get_exception<detail::epitaph_non_value>(eptr));
      EXPECT_TRUE(get_exception<TinyErr>(eptr));
      EXPECT_TRUE(get_exception<rich_error<TinyErr>>(eptr));
    }
    checkFormatViaGet<TinyErr, rich_error_base, std::exception>(eos, re);

    // Epitaphs are transparent: although `epitaph_non_value` is itself a
    // `rich_error_base`, all public APIs (except `get_outer_exception()`) go to
    // the underlying error.
    auto rex = get_rich_error(eos);
    EXPECT_FALSE(dynamic_cast<const detail::epitaph_non_value*>(rex.get()));
    EXPECT_TRUE(dynamic_cast<const TinyErr*>(rex.get()));
    EXPECT_TRUE(dynamic_cast<const rich_error<TinyErr>*>(rex.get()));

    // Dereferencing `rich_ptr_to_underlying_error` discards epitaphs
    EXPECT_EQ(bare_msg, fmt::format("{}", *rex));

    // Cover the underlying `rich_error_base` interface (except codes)
    EXPECT_EQ(bare_msg, rex->partial_message());
    EXPECT_EQ(
        source_location{}.file_name(), rex->source_location().file_name());
    EXPECT_TRUE(nullptr == rex->next_error_for_epitaph());
    EXPECT_TRUE(nullptr == rex->underlying_error());
    EXPECT_TRUE(std::nullopt == get_rich_error_code<A1>(*rex));
  };

  error_or_stopped eos0{rich_error<TinyErr>{}};
  const auto* innerErr = get_exception<TinyErr>(eos0).get();

  // Cover the "outer" `rich_error_base` interface (except codes)
  auto assertOuter = [&](auto& eos, int err_line, const char* msg = "") {
    auto& outer =
        *std::move(eos)
             .release_rich_exception_ptr()
             .template get_outer_exception<detail::epitaph_non_value>();
    EXPECT_EQ(err_line, outer.source_location().line());
    EXPECT_STREQ(msg, outer.partial_message());
    EXPECT_TRUE(nullptr != outer.next_error_for_epitaph());
    EXPECT_EQ(
        innerErr,
        outer.underlying_error()->template get_outer_exception<TinyErr>());
    EXPECT_TRUE(std::nullopt == get_rich_error_code<A1>(outer));
  };

  // Add and test 1 epitaph wrapper
  auto err_line1 = source_location::current().line() + 1;
  auto eos1 = epitaph(std::move(eos0));

  assertOuter(eos1, err_line1);
  assertUnderlying(
      eos1,
      fmt::format("{} \\[via\\] {}:{}", bare_msg, test_file_name, err_line1));

  // Wrap a second epitaph wrapper to ensure skip-to-underlying, not
  // skip-to-next
  auto err_line2 = source_location::current().line() + 1;
  auto eos2 = epitaph(std::move(eos1), "literal");
  assertOuter(eos2, err_line2, "literal");
  assertUnderlying(
      eos2,
      fmt::format(
          "{} \\[via\\] literal @ {}:{} \\[after\\] {}:{}",
          bare_msg,
          test_file_name,
          err_line2,
          test_file_name,
          err_line1));
}

// The underlying error doesn't have to be a `rich_error_base`.
TEST(EpitaphTest, addEpitaphsToPlainError) {
  auto eos = epitaph(
      epitaph(epitaph(error_or_stopped{std::logic_error{"inner"}}), "msg"),
      "format{}",
      std::string{"ted"});

  // `get_exception` makes a `logic_error*` quack-alike with rich formatting
  const std::logic_error* ex{get_exception<std::logic_error>(eos)};
  EXPECT_STREQ("inner", ex->what());
  checkFormatViaGet<std::logic_error, std::exception>(
      eos,
      fmt::format(
          "std::logic_error: inner \\[via\\] formatted @ {}:[0-9]+ \\[after\\] "
          "msg @ {}:[0-9]+ \\[after\\] {}:[0-9]+",
          test_file_name,
          test_file_name,
          test_file_name));
}

} // namespace folly

struct FatalToFormat {}; // Checks that format args are lazily evaluated.
template <>
struct fmt::formatter<FatalToFormat> : fmt::formatter<std::string_view> {
  template <typename Ctx>
  auto format(const FatalToFormat&, Ctx& ctx) const {
    LOG(FATAL) << "FatalToFormat fmt";
    return ctx.out();
  }
};

namespace folly {

// No effect on value-state `result`
TEST(EpitaphTest, addEpitaphsToResultWithValue) {
  EXPECT_DEATH((void)fmt::format("{}", FatalToFormat{}), "FatalToFormat fmt");
  EXPECT_EQ(42, epitaph(result{42}, "{}", FatalToFormat{}).value_or_throw());
  epitaph(result<>{}, "{}", FatalToFormat{}).value_or_throw();
}

// Add epitaphs to a non-value `result`
TEST(EpitaphTest, addEpitaphsToResultWithNonValue) {
  auto r =
      epitaph(result<int>{error_or_stopped{std::logic_error{"inner"}}}, "msg");
  static_assert(std::same_as<result<int>, decltype(r)>);
  EXPECT_FALSE(r.has_value());

  // `get_exception` makes a `logic_error*` quack-alike with rich formatting
  const std::logic_error* ex{get_exception<std::logic_error>(r)};
  EXPECT_STREQ("inner", ex->what());
  checkFormatViaGet<std::logic_error, std::exception>(
      r,
      fmt::format(
          "std::logic_error: inner \\[via\\] msg @ {}:[0-9]+", test_file_name));
}

// Codes are forwarded to the underlying error, and formatted.
TEST(EpitaphTest, retrieveCodeDelegatesToUnderlying) {
  auto eos =
      epitaph(epitaph(error_or_stopped{make_coded_rich_error(A1::ONE_A1)}));

  // If we insist on inspecting the outer error (I'm not seeing how this would
  // happen in normal usage), it has no code. Yes, we could easily forward the
  // code query to underlying -- but during formatting, that would cause every
  // epitaph wrapper in the stack to confusingly show the same code.
  EXPECT_EQ(
      std::nullopt,
      get_rich_error_code<A1>(
          *copy(eos)
               .release_rich_exception_ptr()
               .get_outer_exception<detail::epitaph_non_value>()));

  // Normal access first resolves the underlying error, so code access works.
  EXPECT_EQ(A1::ONE_A1, get_rich_error_code<A1>(eos));

  // The `get_exception` pointer formats with epitaphs, including the code.
  checkFormatViaGet<coded_rich_error<A1>, rich_error_base, std::exception>(
      eos,
      fmt::format(
          "A1=1 @ {}:[0-9]+ \\[via\\] {}:[0-9]+ \\[after\\] {}:[0-9]+",
          test_file_name,
          test_file_name,
          test_file_name));
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
