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

#include <folly/result/immortal_rich_error.h>

#include <folly/result/epitaph.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_msg.h>
#include <folly/result/test/common.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_RESULT

namespace folly::detail {

using namespace folly::string_literals;

const auto test_file_name = source_location::current().file_name();

struct MyErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<MyErr>;

  using rich_error_base::rich_error_base;
  explicit constexpr MyErr(rich_msg msg) : msg_{std::move(msg)} {}
  // This is a demo of the only *current* way of capturing source location in
  // an immortal.  See `future_ideas.md` for a discussion of better ways.
  //
  // To plumb it into `rich_msg` we'd have to give it a new ctor, but since
  // this doesn't seem practically useful, I'm leaving it as demoware.
  constexpr MyErr(rich_msg msg, const folly::source_location*)
      : msg_{std::move(msg)} {}

  constexpr const char* partial_message() const noexcept override {
    return msg_.message();
  }

  // NB: Since we immortals currently don't use `source_location` (see ctor
  // above), it would reduce binary size for an immortal-only error to use
  // `const char*`, like you can see below in `ConstErrWithNext`.
  //
  // Using `rich_msg` showcases an error definition that is equally usable with
  // immortal and dynamic instances.  `coded_rich_error` & friends all follow
  // this pattern.
  rich_msg msg_{"default MyErr"_litv};
};

struct ManualChecks {
  struct BadErr {};
  static constexpr detail::immortal_rich_error_storage<MyErr> my_err;
  static constexpr BadErr bad_err;
  static constexpr bool test() {
#if 0 // `T` derives from `rich_error_base` for `immortal_rich_error<T>`
  // NB: `rich_error_test.cpp` adequately covers the other cases from
  // `static_assert_is_valid_rich_error_type`.
  (void)immortal_rich_error<BadErr>;
#else
    (void)bad_err;
#endif
    return true;
  }
};
static_assert(ManualChecks::test());

// Note: these interfaces have much more exhaustive, albeit non-`constexpr`
// tests in `rich_exception_ptr_fundamentals_test.cpp`.
constexpr bool testCopyMoveAndAccess(
    auto rep, auto repToo, std::string_view msg) {
  // Check the static `immortal_rich_error_storage`. See `copyMoveAndAccess` for
  // leaky singleton tests since those use non-`constexpr` `std::exception`

  auto rex = get_rich_error(rep);
  test(msg == rex->partial_message());

  // Typing `immortal_rich_error<...>` with the same args in the same TU does
  // NOT give you a brand new pointer.
  test(rex == get_rich_error(repToo));
  {
    auto otherRep = immortal_rich_error<rich_error<MyErr>, "other"_litv>.ptr();
    test(rex != get_rich_error(otherRep));
  }

  auto userEx = get_exception<MyErr>(rep);
  test(msg == userEx->partial_message());
  test(rex == static_cast<const rich_error_base*>(userEx.get()));

  { // Copying the `rich_exception_ptr` preserves the pointers
    auto rep2 = rep;
    test(rex == get_rich_error(rep2));
    test(userEx == get_exception<MyErr>(rep2));
  }
  { // Moving the `rich_exception_ptr` preserves the pointers
    auto rep3 = std::move(rep);

    // NOLINTNEXTLINE(bugprone-use-after-move)
    test(rich_exception_ptr{} == rep);

    test(rex == get_rich_error(rep3));
    test(userEx == get_exception<MyErr>(rep3));
  }

  return true;
}
// Showcase the various ways of constructin immortals.
static_assert(testCopyMoveAndAccess(
    immortal_rich_error<rich_error<MyErr>>.ptr(),
    immortal_rich_error<rich_error<MyErr>>.ptr(), // same
    "default MyErr"));
static_assert(testCopyMoveAndAccess(
    immortal_rich_error<MyErr, "howdy"_litv>.ptr(),
    immortal_rich_error<MyErr, "howdy"_litv>.ptr(), // same
    "howdy"));
static_assert(testCopyMoveAndAccess(
    immortal_rich_error<MyErr>.ptr(),
    immortal_rich_error<MyErr>.ptr(), // same
    "default MyErr"));
static_assert(testCopyMoveAndAccess(
    immortal_rich_error<rich_error<MyErr>, "howdy"_litv>.ptr(),
    immortal_rich_error<rich_error<MyErr>, "howdy"_litv>.ptr(), // same
    "howdy"));
// See the corresponding `MyErr` ctor for the rationale.
inline constexpr auto testSrcLoc = source_location::current();
static_assert(testCopyMoveAndAccess(
    immortal_rich_error<MyErr, "hi"_litv, &testSrcLoc>.ptr(),
    immortal_rich_error<MyErr, "hi"_litv, &testSrcLoc>.ptr(), // same
    "hi"));

// Extends `testCopyMoveAndAccess` to cover non-`constexpr` functionality like
// `std::exception` and formatting.  I didn't repeat the ctor test matrix since
// the `constexpr` coverage should be sufficient.
//
// Note that `rich_exception_ptr_immortal_test.cpp` covers the basic pointer
// behaviors far more thoroughly. This is a usage-oriented test.
TEST(ImmortalRichErrorTest, copyMoveAndAccess) {
  auto rep = immortal_rich_error<rich_error<MyErr>>.ptr();
  std::string_view msg{"default MyErr"};

  // These 2 check the static `immortal_rich_error_storage`:

  auto rex = get_rich_error(rep);
  EXPECT_EQ(msg, rex->partial_message());
  checkFormat(rex, std::string{msg});

  auto userEx = get_exception<MyErr>(rep);
  EXPECT_EQ(msg, userEx->partial_message());
  checkFormat(userEx, std::string{msg});
  EXPECT_EQ(rex, static_cast<const rich_error_base*>(userEx.get()));

  // The next 2 use the immutable `rich_error` leaky Meyer singleton:

  auto stdEx = get_exception<std::exception>(rep);
  EXPECT_EQ(msg, stdEx->what());
  checkFormat(stdEx, std::string{msg});

  auto leafEx = get_exception<rich_error<MyErr>>(rep);
  EXPECT_EQ(msg, leafEx->partial_message());
  EXPECT_EQ(msg, leafEx->what());
  checkFormat(leafEx, std::string{msg});
  EXPECT_EQ(stdEx, static_cast<const std::exception*>(leafEx.get()));
  EXPECT_NE(userEx, static_cast<const MyErr*>(leafEx.get()));
  EXPECT_NE(rex, static_cast<const rich_error_base*>(leafEx.get()));

  // The next 2 use the mutable `rich_error` leaky singleton:

  auto mutStdEx = get_mutable_exception<std::exception>(rep);
  EXPECT_EQ(msg, mutStdEx->what());
  checkFormat(mutStdEx, std::string{msg});
  // Future: extend `operator==` to drop the `get()` here.
  EXPECT_NE(stdEx, mutStdEx.get());

  auto mutLeafEx = get_mutable_exception<rich_error<MyErr>>(rep);
  EXPECT_EQ(msg, mutLeafEx->partial_message());
  EXPECT_EQ(msg, mutLeafEx->what());
  checkFormat(mutLeafEx, std::string{msg});
  EXPECT_EQ(mutStdEx, static_cast<std::exception*>(mutLeafEx.get()));
  // Future: extend `operator==` to drop the `get()` here.
  EXPECT_NE(leafEx, mutLeafEx.get());
  EXPECT_NE(stdEx, static_cast<const std::exception*>(mutLeafEx.get()));
  EXPECT_NE(userEx, static_cast<const MyErr*>(mutLeafEx.get()));
  EXPECT_NE(rex, static_cast<const rich_error_base*>(mutLeafEx.get()));

  { // Copying the `rich_exception_ptr` preserves the pointers
    auto rep2 = rep;
    EXPECT_EQ(rex, get_rich_error(rep2));
    EXPECT_EQ(userEx, get_exception<MyErr>(rep2));
    EXPECT_EQ(stdEx, get_exception<std::exception>(rep2));
    EXPECT_EQ(leafEx, get_exception<rich_error<MyErr>>(rep2));
    EXPECT_EQ(mutStdEx, get_mutable_exception<std::exception>(rep2));
    EXPECT_EQ(mutLeafEx, get_mutable_exception<rich_error<MyErr>>(rep2));
  }
  { // Moving the `rich_exception_ptr` preserves the pointers
    auto rep3 = std::move(rep);

    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(rich_exception_ptr{}, rep);

    EXPECT_EQ(rex, get_rich_error(rep3));
    EXPECT_EQ(userEx, get_exception<MyErr>(rep3));
    EXPECT_EQ(stdEx, get_exception<std::exception>(rep3));
    EXPECT_EQ(leafEx, get_exception<rich_error<MyErr>>(rep3));
    EXPECT_EQ(mutStdEx, get_mutable_exception<std::exception>(rep3));
    EXPECT_EQ(mutLeafEx, get_mutable_exception<rich_error<MyErr>>(rep3));
  }
}

TEST(ImmortalRichErrorTest, formatWithEpitaphs) {
  auto rep = immortal_rich_error<MyErr>.ptr();
  EXPECT_EQ("default MyErr", fmt::format("{}", get_rich_error(rep)));

  auto err_line = source_location::current().line() + 1;
  rich_error<detail::epitaph_non_value> err{std::move(rep), rich_msg{"msg"}};

  checkFormatOfErrAndRep<MyErr, rich_error_base, std::exception>(
      err,
      fmt::format(
          "default MyErr \\[via\\] msg @ {}:{}", test_file_name, err_line));
}

struct ConstErrWithNext : rich_error_base {
  // Note that if your error also has dynamic `rich_error` instances and not
  // just immortals, you may want `rich_msg` here, see `MyErr` comments above.
  const char* msg_;
  const rich_exception_ptr* next_;

  template <literal_string S>
  explicit consteval ConstErrWithNext(
      vtag_t<S>, const rich_exception_ptr* next = nullptr)
      : msg_{S.c_str()}, next_{next} {}

  const char* partial_message() const noexcept override { return msg_; }
  const rich_exception_ptr* next_error_for_epitaph() const noexcept override {
    return next_;
  }

  using folly_get_exception_hint_types = rich_error_hints<ConstErrWithNext>;
};

// The main points of this test are:
//  - Immortal errors taking nontrivial ctor arguments, including by-ptr.
//  - Simple demo of the `_litv` setup used for feeding string literals
//    into immortals taking `rich_msg`.
//
// While the practical value is unclear, this also shows that it *is* possible
// to nest `immortal_rich_error`s, by using a particular constructor setup
// (`_litv` for strings, pointers for nested).
TEST(ImmortalRichErrorTest, formatAndQueryNested) {
  static constexpr rich_exception_ptr inner_ptr =
      immortal_rich_error<ConstErrWithNext, "inner"_litv>.ptr();
  static constexpr rich_exception_ptr middle_ptr =
      immortal_rich_error<ConstErrWithNext, "middle"_litv, &inner_ptr>.ptr();
  static constexpr rich_exception_ptr outer_ptr =
      immortal_rich_error<ConstErrWithNext, "outer"_litv, &middle_ptr>.ptr();

  auto re = "outer \\[after\\] middle \\[after\\] inner";
  checkFormatViaGet<ConstErrWithNext, rich_error_base, std::exception>(
      outer_ptr, re);

  EXPECT_STREQ("outer", get_rich_error(outer_ptr)->partial_message());
  EXPECT_STREQ("outer", get_exception<std::exception>(outer_ptr)->what());
}

// Cover all the immortal-related comparisons for `rich_exception_ptr`.
//
// Note: this is a bit redundant with `rich_exception_ptr_fundamentals_test.cpp`
TEST(ImmortalRichErrorTest, compareRichExceptionPtrs) {
  rich_exception_ptr empty{};

  auto repA = immortal_rich_error<rich_error<MyErr>>.ptr();
  EXPECT_EQ(repA, repA);
  EXPECT_NE(repA, empty);
  EXPECT_NE(empty, repA);

  struct OtherErr : rich_error_base {
    using folly_get_exception_hint_types = rich_error_hints<OtherErr>;
  };
  auto repB = immortal_rich_error<rich_error<OtherErr>>.ptr();
  EXPECT_EQ(repB, repB);
  EXPECT_NE(repB, empty);
  EXPECT_NE(empty, repB);
  EXPECT_NE(repB, repA);
  EXPECT_NE(repA, repB);

  // This ends up making an "owned" eptr, but it still references the exception
  // object of the mutable leaky singleton, so it compares equal.
  auto repAOwned =
      rich_exception_ptr::from_exception_ptr_slow(repA.to_exception_ptr_slow());
  EXPECT_EQ(repAOwned, repAOwned);
  EXPECT_EQ(repAOwned, repA);
  EXPECT_EQ(repA, repAOwned);
  EXPECT_NE(repAOwned, empty);
  EXPECT_NE(empty, repAOwned);
  EXPECT_NE(repAOwned, repB);
  EXPECT_NE(repB, repAOwned);
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
