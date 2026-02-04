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

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/epitaph.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_error_code.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

#include <stdexcept>

#if FOLLY_HAS_RESULT

namespace folly::test {

using namespace folly::detail;

const auto test_file_name = source_location::current().file_name();

constexpr bool manually_test_asserts() {
#if 0 // Rich error derives from `rich_error_base`
  struct BadErr {};
  (void)rich_error<BadErr>{};
#elif 0 // Rich error type must have a `folly_get_exception_hint_types` hint...
  struct BadErr : rich_error_base {};
  (void)rich_error<BadErr>{};
#elif 0 // ... and the hint must correctly use `rich_error_hints<>`.
  struct BadErr : rich_error_base {
    using folly_get_exception_hint_types = tag_t<BadErr>;
  };
  (void)rich_error<BadErr>{};
#elif 0 // `rich_error_base` has offset 0 in rich error
  struct VirtBase {
    virtual ~VirtBase() {}
  };
  struct BadOffset : VirtBase, rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<BadOffset>;
  };
  (void)rich_error<BadOffset>{};
#endif
  return true;
}
static_assert(manually_test_asserts());

constexpr bool test_has_offset0_base() {
  struct Offset : std::exception {
    virtual ~Offset() = default;
    int z;
  };

  struct Base {
    virtual ~Base() = default;
    virtual void foo() {}
    int x;
  };

  struct DerivedNoOffset : Base {
    double y;
  };

  struct DerivedWithOffset : Offset, Base {
    double y;
  };

  static_assert(has_offset0_base<DerivedNoOffset, Base>);
  static_assert(!has_offset0_base<DerivedWithOffset, Base>);

  return true;
}
static_assert(test_has_offset0_base());

// Check `rich_error_base` API, and formatting.
TEST(RichErrorTest, basics) {
  // These APIs are more meaningfully covered in later tests.
  auto checkNoOpAPIs = [](auto& err) {
    EXPECT_TRUE(nullptr == err.next_error_for_epitaph());
    EXPECT_TRUE(nullptr == err.underlying_error());
    EXPECT_TRUE(std::nullopt == get_rich_error_code<A1>(err));
  };
  { // `partial_message()`, but no location.
    struct MyErr : rich_error_base {
      using folly_get_exception_hint_types = rich_error_hints<MyErr>;
    };
    rich_error<MyErr> err;
    EXPECT_STREQ("MyErr", err.partial_message());
    EXPECT_EQ(source_location{}.file_name(), err.source_location().file_name());
    checkNoOpAPIs(err);
    checkFormatOfErrAndRep<MyErr, rich_error_base, std::exception>(
        err, "MyErr");
  }

  static constexpr source_location loc = source_location::current();
  EXPECT_GT(loc.line(), 0);
  struct ErrWithLoc : rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<ErrWithLoc>;
    folly::source_location source_location() const noexcept override {
      return loc;
    }
  };
  { // Has location, and `partial_message()`.
    rich_error<ErrWithLoc> err;
    EXPECT_STREQ("ErrWithLoc", err.partial_message());
    EXPECT_EQ(loc.line(), err.source_location().line());
    checkNoOpAPIs(err);
    checkFormatOfErrAndRep<ErrWithLoc, rich_error_base, std::exception>(
        err, fmt::format("ErrWithLoc @ {}:{}", test_file_name, loc.line()));
  }
  { // Has location, but no `partial_message()`.
    struct LocOnlyErr : ErrWithLoc {
      using folly_get_exception_hint_types = rich_error_hints<LocOnlyErr>;
      const char* partial_message() const noexcept override { return ""; }
    };
    rich_error<LocOnlyErr> err;
    EXPECT_STREQ("", err.partial_message());
    EXPECT_EQ(loc.line(), err.source_location().line());
    checkNoOpAPIs(err);
    EXPECT_EQ( // The ` @ ` is NOT emitted with `partial_message()` is empty.
      "ErrWithLoc @ " + fmt::format("{}", err),
      fmt::format("{}", rich_error<ErrWithLoc>{}));
  }
}

// Real programs use `coded_rich_error`, `errc_rich_error`, etc.  So, those
// tests have a lot more detail.
TEST(RichErrorTest, fmtAndOstreamWithCode) {
  struct ErrWithCode : rich_error_base {
    C1 code_;
    ErrWithCode() : code_{C1::ONE_C1} {}
    constexpr C1 code() const { return code_; }
    using folly_rich_error_codes_t = rich_error_bases_and_own_codes<
        ErrWithCode,
        tag_t<>,
        &ErrWithCode::code>;
    constexpr void retrieve_code(rich_error_code_query& c) const override {
      return folly_rich_error_codes_t::retrieve_code(*this, c);
    }
    using folly_get_exception_hint_types = rich_error_hints<ErrWithCode>;
  };
  rich_error<ErrWithCode> err;
  EXPECT_TRUE(std::nullopt == get_rich_error_code<A1>(err));
  EXPECT_TRUE(C1::ONE_C1 == get_rich_error_code<C1>(err));
  checkFormatOfErrAndRep<ErrWithCode, rich_error_base, std::exception>(
      err, "ErrWithCode - folly::test::C1=101");
}

// This is a mini-test of `rich_ptr_to_underlying_error` rich formatting.
// It stacks 2 epitaph wrappers to distinguish "next" from "underlying".
// `epitaph_test.cpp` covers this in much more detail.
TEST(RichErrorTest, fmtAndOstreamEpitaphNonRichError) {
  using WrapErr = rich_error<epitaph_non_value>;

  rich_exception_ptr inner{std::logic_error{"inner"}};
  auto err_line1 = source_location::current().line() + 1;
  rich_exception_ptr middle{WrapErr{std::move(inner), rich_msg{"middle"}}};
  auto err_line2 = source_location::current().line() + 1;
  WrapErr outer{std::move(middle), rich_msg{"outer"}};

  // Check the wrapper's `rich_error_base` API -- NOT user-visible without
  // `get_outer_exception` once the wrapper is in `rich_exception_ptr`.
  EXPECT_STREQ("outer", outer.partial_message());
  EXPECT_EQ(err_line2, outer.source_location().line());
  EXPECT_TRUE(nullptr != outer.next_error_for_epitaph());
  EXPECT_NE((void*)&outer, outer.next_error_for_epitaph());
  EXPECT_TRUE(nullptr != outer.underlying_error());
  EXPECT_NE((void*)&outer, outer.underlying_error());
  EXPECT_NE((void*)outer.underlying_error(), outer.next_error_for_epitaph());
  EXPECT_TRUE(std::nullopt == get_rich_error_code<A1>(outer));

  // The error is usefully formattable.  Packing it into `rich_exception_ptr`
  // and querying it returns a quack-a-like of `logic_error*` with the same
  // formatting.  But, the bare exception isn't formattable.
  checkFormatOfErrAndRep<std::logic_error, std::exception>(
      outer,
      fmt::format(
          "std::logic_error: inner \\[via\\] "
          "outer @ {}:{} \\[after\\] middle @ {}:{}",
          test_file_name,
          err_line2,
          test_file_name,
          err_line1));

  rich_exception_ptr rep{std::move(outer)};
  // The wrapper is transparent, so `rep` cannot query `rich_error_base`,
  EXPECT_TRUE(nullptr == get_rich_error(rep));

  auto ex = get_exception<std::logic_error>(rep); // Quacks like `logic_error*`
  EXPECT_STREQ("inner", ex->what());
  static_assert(std::is_same_v<const std::logic_error&, decltype(*ex)>);
  static_assert(
      std::is_same_v<
          rich_ptr_to_underlying_error<const std::logic_error>,
          decltype(ex)>);
}

// What makes this test special is that:
//   - the outer rich error is underlying (no `epitaph` wrapping), and
//   - it has a `next_error_for_epitaph()`.
// (1) We end up with a `rich_ptr` whose `raw_ptr_` and `top_rich_error_` are
//     both the same rich error.  To avoid duplicate output, the formatter
//     needs to handle this specially.
// (2) This is also a minimal test for errors with `std::nested_exception`-like
//     behavior (real example in `nestable_coded_rich_error.h`).
TEST(RichErrorBaseFormatTest, unwrappedUnderlyingErrorHasNext) {
  struct ErrWithNext : rich_error_base {
    const char* msg_;
    rich_exception_ptr next_;

    explicit ErrWithNext(const char* msg) : msg_{msg} {}
    explicit ErrWithNext(const char* msg, rich_exception_ptr next)
        : msg_{msg}, next_{std::move(next)} {}

    const char* partial_message() const noexcept override { return msg_; }
    const rich_exception_ptr* next_error_for_epitaph() const noexcept override {
      return next_ != rich_exception_ptr{} ? &next_ : nullptr;
    }

    using folly_get_exception_hint_types = rich_error_hints<ErrWithNext>;
  };

  rich_error<ErrWithNext> err{
      "Err2", // new underlying
      rich_exception_ptr{rich_error<epitaph_non_value>{
          rich_exception_ptr{rich_error<ErrWithNext>{"Err1"}}, // old underlying
          rich_msg{"msg"}}}}; // epitaph wrapper around old
  checkFormatOfErrAndRep<ErrWithNext, rich_error_base, std::exception>(
      err,
      fmt::format(
          "Err2 \\[after\\] Err1 \\[via\\] msg @ {}:[0-9]+", test_file_name));
}

} // namespace folly::test

// rich_ptr even knows to format underlying errors that are not rich
struct FormattableNonRich : std::exception {
  const char* what() const noexcept override {
    return "FormattableNonRich::what";
  }
};
template <>
struct fmt::formatter<FormattableNonRich> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const FormattableNonRich& e, format_context& ctx) const {
    return fmt::format_to(ctx.out(), "(my {})", e.what());
  }
};

namespace folly::test {

// When an error is formattable and not `rich_error_base`, we first format that
// error AND then append the rich error context (like epitaph wrappers).
TEST(RichErrorTest, formattableExFormatter) {
  rich_error<epitaph_non_value> err{
      rich_exception_ptr{FormattableNonRich{}}, rich_msg{"msg"}};
  // No custom format "(my ...)", since the underlying error is type-erased
  checkFormatOfErrAndRep<std::exception>(
      err, // This underlying is NOT rich!
      fmt::format(
          "FormattableNonRich: FormattableNonRich::what \\[via\\] "
          "msg @ {}:[0-9]+",
          test_file_name));

  rich_exception_ptr rep{std::move(err)};
  // Uses the custom format since we query a formattable type.
  checkFormat(
      get_exception<FormattableNonRich>(rep),
      fmt::format(
          "\\(my FormattableNonRich::what\\) \\[via\\] msg @ {}:[0-9]+",
          test_file_name));
}

TEST(RichErrorTest, formatNullRichPtr) {
  auto check_null_rich_ptr = []<typename Query>(tag_t<Query>) {
    rich_exception_ptr rep{std::logic_error{"bad"}};
    auto null_ptr = get_exception<Query>(rep);
    EXPECT_FALSE(null_ptr);
    EXPECT_EQ(nullptr, null_ptr.get());
    checkFormat(null_ptr, "\\[nullptr folly::rich_ptr_to_underlying_error\\]");
  };
  // Specialization 1: formattable non-rich error
  check_null_rich_ptr(tag<FormattableNonRich>);
  // Specialization 2: rich error OR non-formattable error
  check_null_rich_ptr(tag<std::runtime_error>);
}

TEST(RichErrorTest, customFormatTo) {
  struct CustomFormatErr : rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<CustomFormatErr>;
    void format_to(fmt::appender& out) const override {
      fmt::format_to(out, "custom: ");
      rich_error_base::format_to(out);
    }
  };
  checkFormatOfErrAndRep<CustomFormatErr, rich_error_base, std::exception>(
      rich_error<CustomFormatErr>{}, "custom: CustomFormatErr");
}

TEST(RichErrorTest, whatFallbackToPrettyName) {
  struct EmptyMsgErr : rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<EmptyMsgErr>;
    const char* partial_message() const noexcept override { return ""; }
  };
  rich_error<EmptyMsgErr> err;
  EXPECT_STREQ("", err.partial_message());
  // With empty `partial_message()`, `what()` should still be informative.
  EXPECT_STREQ(pretty_name<rich_error<EmptyMsgErr>>(), err.what());
}

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
