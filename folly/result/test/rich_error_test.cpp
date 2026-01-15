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
#include <folly/result/rich_error.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/test/common.h>

#include <stdexcept>

#if FOLLY_HAS_RESULT

namespace folly {

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

  static_assert(detail::has_offset0_base<DerivedNoOffset, Base>);
  static_assert(!detail::has_offset0_base<DerivedWithOffset, Base>);

  return true;
}
static_assert(test_has_offset0_base());

// Check `rich_error_base` API, and formatting.
TEST(RichErrorTest, basics) {
  // These APIs are more meaningfully covered in later tests.
  auto checkNoOpAPIs = [](auto& err) {
    EXPECT_TRUE(nullptr == err.next_error_for_enriched_message());
    EXPECT_TRUE(nullptr == err.underlying_error());
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

} // namespace folly

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

namespace folly {

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

} // namespace folly

#endif // FOLLY_HAS_RESULT
