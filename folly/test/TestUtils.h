/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

/*
 * This file contains additional gtest-style check macros to use in unit tests.
 *
 * - SKIP(), SKIP_IF(EXPR)
 * - EXPECT_THROW_RE(), ASSERT_THROW_RE()
 * - EXPECT_THROW_ERRNO(), ASSERT_THROW_ERRNO()
 * - AreWithinSecs()
 *
 * It also imports PrintTo() functions for StringPiece, FixedString and
 * FBString. Including this file in your tests will ensure that they are printed
 * as strings by googletest - for example in failing EXPECT_EQ() checks.
 */

#include <chrono>
#include <regex>
#include <system_error>
#include <type_traits>

#include <folly/Conv.h>
#include <folly/ExceptionString.h>
#include <folly/FBString.h>
#include <folly/FixedString.h>
#include <folly/Range.h>
#include <folly/portability/GTest.h>

// SKIP() is used to mark a test skipped if we could not successfully execute
// the test due to runtime issues or behavior that do not necessarily indicate
// a problem with the code.
//
// It used to be that `googletest` did NOT have a built-in mechanism to
// report tests as skipped a run time.  As of the following commit, it has
// fairly complete support for the feature -- i.e.  `SKIP` does what you
// expect both in test fixture `SetUp` and in `Environment::SetUp`, as well
// as in the test body:
//
// https://github.com/google/googletest/commit/67c75ff8baf4228e857c09d3aaacd3f1ddf53a8f
//
// Open-source projects depending on folly may still use the latest release
// googletest-1.8.1, which does not have that feature.  Therefore, for
// backwards compatibility, our `SKIP` only uses `GTEST_SKIP_` when
// available.
//
// Difference from vanilla `GTEST_SKIP`: The skip message diverges from
// upstream's "Skipped" in order to conform with the historical message in
// `folly`.  The intention is not to break tooling that depends on that
// specific pattern.
//
// Differences between the new `GTEST_SKIP_` path and the old
// `GTEST_MESSAGE_` path:
//   - New path: `SKIP` in `SetUp` prevents the test from running.  Running
//     the gtest main directly clearly marks SKIPPED tests.
//   - Old path: Without `skipIsFailure`, `SKIP` in `SetUp` would
//     unexpectedly execute the test, potentially causing segfaults or
//     undefined behavior.  We would either report the test as successful or
//     failed based on whether the `FOLLY_SKIP_AS_FAILURE` environment
//     variable is set.  The default is to report the test as successful.
//     Enabling FOLLY_SKIP_AS_FAILURE was to be used with a test harness
//     that can identify the "Test skipped by client" in the failure message
//     and convert this into a skipped test result.
#ifdef GTEST_SKIP_
#define SKIP(msg) GTEST_SKIP()
#else
#define SKIP()                                       \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                      \
  return GTEST_MESSAGE_(                             \
      "Test skipped by client",                      \
      ::folly::test::detail::skipIsFailure()         \
          ? ::testing::TestPartResult::kFatalFailure \
          : ::testing::TestPartResult::kSuccess)
#endif

// Encapsulate conditional-skip, since it's nontrivial to get right.
#define SKIP_IF(expr)           \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_ \
  if (!(expr)) {                \
  } else                        \
    SKIP()

#define TEST_THROW_ERRNO_(statement, errnoValue, fail)       \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                              \
  if (::folly::test::detail::CheckResult gtest_result =      \
          ::folly::test::detail::checkThrowErrno(            \
              [&] { statement; }, errnoValue, #statement)) { \
  } else                                                     \
    fail(gtest_result.what())

/**
 * Check that a statement throws a std::system_error with the expected errno
 * value.  This is useful for checking code that uses the functions in
 * folly/Exception.h to throw exceptions.
 *
 * Like other EXPECT_* and ASSERT_* macros, additional message information
 * can be included using the << stream operator.
 *
 * Example usage:
 *
 *   EXPECT_THROW_ERRNO(readFile("notpresent.txt"), ENOENT)
 *     << "notpresent.txt should not exist";
 */
#define EXPECT_THROW_ERRNO(statement, errnoValue) \
  TEST_THROW_ERRNO_(statement, errnoValue, GTEST_NONFATAL_FAILURE_)
#define ASSERT_THROW_ERRNO(statement, errnoValue) \
  TEST_THROW_ERRNO_(statement, errnoValue, GTEST_FATAL_FAILURE_)

#define TEST_THROW_RE_(statement, exceptionType, pattern, fail)           \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                                           \
  if (::folly::test::detail::CheckResult gtest_result =                   \
          ::folly::test::detail::checkThrowRegex<exceptionType>(          \
              [&] { statement; }, pattern, #statement, #exceptionType)) { \
  } else                                                                  \
    fail(gtest_result.what())

/**
 * Check that a statement throws the expected exception type, and that the
 * exception message matches the specified regular expression.
 *
 * Partial matches (against just a portion of the error message) are accepted
 * if the regular expression does not explicitly start with "^" and end with
 * "$".  (The matching is performed using std::regex_search() rather than
 * std::regex_match().)
 *
 * This uses ECMA-262 style regular expressions (the default behavior of
 * std::regex).
 *
 * Like other EXPECT_* and ASSERT_* macros, additional message information
 * can be included using the << stream operator.
 *
 * Example usage:
 *
 *   EXPECT_THROW_RE(badFunction(), std::runtime_error, "oh noes")
 *     << "function did not throw the expected exception";
 */
#define EXPECT_THROW_RE(statement, exceptionType, pattern) \
  TEST_THROW_RE_(statement, exceptionType, pattern, GTEST_NONFATAL_FAILURE_)
#define ASSERT_THROW_RE(statement, exceptionType, pattern) \
  TEST_THROW_RE_(statement, exceptionType, pattern, GTEST_FATAL_FAILURE_)

namespace folly {
namespace test {

template <typename T1, typename T2>
::testing::AssertionResult
AreWithinSecs(T1 val1, T2 val2, std::chrono::seconds acceptableDeltaSecs) {
  auto deltaSecs =
      std::chrono::duration_cast<std::chrono::seconds>(val1 - val2);
  if (deltaSecs <= acceptableDeltaSecs &&
      deltaSecs >= -1 * acceptableDeltaSecs) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure()
        << val1.count() << " and " << val2.count() << " are not within "
        << acceptableDeltaSecs.count() << " secs of each other";
  }
}

namespace detail {

inline bool skipIsFailure() {
  const char* p = getenv("FOLLY_SKIP_AS_FAILURE");
  return p && (0 == strcmp(p, "1"));
}

/**
 * Helper class for implementing test macros
 */
class CheckResult {
 public:
  explicit CheckResult(bool s) noexcept : success_(s) {}

  explicit operator bool() const noexcept {
    return success_;
  }
  const char* what() const noexcept {
    return message_.c_str();
  }

  /**
   * Support the << operator for building up the error message.
   *
   * The arguments are treated as with folly::to<string>(), and we do not
   * support iomanip parameters.  The main reason we use the << operator
   * as opposed to a variadic function like folly::to is that clang-format
   * formats long statements using << much nicer than function call arguments.
   */
  template <typename T>
  CheckResult& operator<<(T&& t) {
    toAppend(std::forward<T>(t), &message_);
    return *this;
  }

 private:
  bool success_;
  std::string message_;
};

/**
 * Helper function for implementing EXPECT_THROW
 */
template <typename Fn>
CheckResult checkThrowErrno(Fn&& fn, int errnoValue, const char* statementStr) {
  try {
    fn();
  } catch (const std::system_error& ex) {
    // TODO: POSIX errno values should use std::generic_category(), but
    // folly/Exception.h incorrectly throws them using std::system_category()
    // at the moment.
    // For now we also accept std::system_category so that we will also handle
    // exceptions from folly/Exception.h correctly.
    if (ex.code().category() != std::generic_category() &&
        ex.code().category() != std::system_category()) {
      return CheckResult(false)
          << "Expected: " << statementStr << " throws an exception with errno "
          << errnoValue << " (" << std::generic_category().message(errnoValue)
          << ")\nActual: it throws a system_error with category "
          << ex.code().category().name() << ": " << ex.what();
    }
    if (ex.code().value() != errnoValue) {
      return CheckResult(false)
          << "Expected: " << statementStr << " throws an exception with errno "
          << errnoValue << " (" << std::generic_category().message(errnoValue)
          << ")\nActual: it throws errno " << ex.code().value() << ": "
          << ex.what();
    }
    return CheckResult(true);
  } catch (const std::exception& ex) {
    return CheckResult(false)
        << "Expected: " << statementStr << " throws an exception with errno "
        << errnoValue << " (" << std::generic_category().message(errnoValue)
        << ")\nActual: it throws a different exception: " << exceptionStr(ex);
  } catch (...) {
    return CheckResult(false)
        << "Expected: " << statementStr << " throws an exception with errno "
        << errnoValue << " (" << std::generic_category().message(errnoValue)
        << ")\nActual: it throws a non-exception type";
  }
  return CheckResult(false)
      << "Expected: " << statementStr << " throws an exception with errno "
      << errnoValue << " (" << std::generic_category().message(errnoValue)
      << ")\nActual: it throws nothing";
}

/**
 * Helper function for implementing EXPECT_THROW_RE
 */
template <typename ExType, typename Fn>
CheckResult checkThrowRegex(
    Fn&& fn,
    const char* pattern,
    const char* statementStr,
    const char* excTypeStr) {
  static_assert(
      std::is_base_of<std::exception, ExType>::value,
      "EXPECT_THROW_RE() exception type must derive from std::exception");

  try {
    fn();
  } catch (const std::exception& ex) {
    const auto* derived = dynamic_cast<const ExType*>(&ex);
    if (!derived) {
      return CheckResult(false)
          << "Expected: " << statementStr << "throws a " << excTypeStr
          << ")\nActual: it throws a different exception type: "
          << exceptionStr(ex);
    }

    std::regex re(pattern);
    if (!std::regex_search(derived->what(), re)) {
      return CheckResult(false)
          << "Expected: " << statementStr << " throws a " << excTypeStr
          << " with message matching \"" << pattern
          << "\"\nActual: message is: " << derived->what();
    }
    return CheckResult(true);
  } catch (...) {
    return CheckResult(false)
        << "Expected: " << statementStr << " throws a " << excTypeStr
        << ")\nActual: it throws a non-exception type";
  }
  return CheckResult(false) << "Expected: " << statementStr << " throws a "
                            << excTypeStr << ")\nActual: it throws nothing";
}

} // namespace detail
} // namespace test

// Define PrintTo() functions for StringPiece/FixedString/fbstring, so that
// gtest checks will print them as strings.  Without these gtest identifies them
// as container types, and therefore tries printing the elements individually,
// despite the fact that there is an ostream operator<<() defined for each of
// them.
//
// gtest's PrintToString() function is used to quote the string and escape
// internal quotes and non-printable characters, the same way gtest does for the
// string types it directly supports.
inline void PrintTo(StringPiece const& stringPiece, std::ostream* out) {
  *out << ::testing::PrintToString(stringPiece.str());
}

inline void PrintTo(
    Range<wchar_t const*> const& stringPiece,
    std::ostream* out) {
  *out << ::testing::PrintToString(
      std::wstring(stringPiece.begin(), stringPiece.size()));
}

template <typename CharT, size_t N>
void PrintTo(
    BasicFixedString<CharT, N> const& someFixedString,
    std::ostream* out) {
  *out << ::testing::PrintToString(someFixedString.toStdString());
}

template <typename CharT, class Storage>
void PrintTo(
    basic_fbstring<
        CharT,
        std::char_traits<CharT>,
        std::allocator<CharT>,
        Storage> const& someFbString,
    std::ostream* out) {
  *out << ::testing::PrintToString(someFbString.toStdString());
}
} // namespace folly
