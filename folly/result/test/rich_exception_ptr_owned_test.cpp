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

#include <folly/result/test/rich_exception_ptr_check_get.h>

#include <folly/portability/GTest.h>

// Tests for owned `rich_exception_ptr` (dynamic exceptions on heap), covering
// exception queries & `throw_exception`. Complements the tests in
// `rich_exception_ptr_fundamental_test.cpp`. Look at `rich_error_test.cpp` for
// usage-oriented tests.

#if FOLLY_HAS_RESULT

namespace folly::test {

using namespace folly::detail;

// For owned errors, aims to cover:
//  - `get_exception` and `get_outer_exception`
//  - rich and non-rich errors + epitaph wrappers
//  - queries touching all relevant branches of `get_exception_impl`
template <typename REP>
void checkAccessException() {
  // Rich error handling matches immortals
  checkGetExceptionForRichErr(REP{rich_error<RichErr>{}});
  checkGetExceptionForEpitaphRichErr<REP>(
      // We cannot use `REP` for the inner error, since `underlying_error()` is
      // always `rich_exception_ptr`.
      rich_exception_ptr{rich_error<RichErr>{}});

  { // Non-rich error
    REP rep{std::logic_error{"test"}};
    checkGetExceptionBoth<
        GetExceptionResult{.isHit = false},
        rich_error_base,
        RichErr,
        rich_error<RichErr>>(rep);
    checkGetExceptionBoth<
        GetExceptionResult{.isHit = true},
        std::exception,
        std::logic_error>(rep);
  }
  { // Non-rich error with epitaph wrapper
    REP rep{rich_error<epitaph_non_value>{
        // We cannot use `REP` for the inner error, since `underlying_error()`
        // is always `rich_exception_ptr`.
        rich_exception_ptr{std::logic_error{"test"}},
        rich_msg{"msg"}}};
    // Epitaphs are transparent -- e.g. `get_exception<rich_error_base>` misses
    checkGetException<
        GetExceptionResult{.isHit = false},
        rich_error_base,
        RichErr,
        epitaph_non_value,
        rich_error<epitaph_non_value>>(rep);
    // `get_exception<underlying error>` should hit, with `top_rich_error_` set
    checkGetException<
        GetExceptionResult{.isHit = true, .hitHasTopRichError = true},
        std::exception,
        std::logic_error>(rep);
    // `get_outer_exception` sees epitaph wrapper, not the underlying error
    checkGetOuterException<
        GetExceptionResult{.isHit = false},
        std::logic_error>(rep);
    checkGetOuterException<
        GetExceptionResult{.isHit = true},
        std::exception,
        rich_error_base,
        epitaph_non_value,
        rich_error<epitaph_non_value>>(rep);
  }
}

TEST(RichExceptionPtrOwned, accessException) {
  checkAccessException<rich_exception_ptr>();
}
TEST(RichExceptionPtrOwned, accessExceptionSeparate) {
  checkAccessException<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtrOwned, accessExceptionPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkAccessException<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkThrowExceptionOwned() {
  auto checkThrow = [&](auto error, auto... args) {
    REP rep{std::move(error)};
    REP rep_copy{rep}; // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_THROW(rep.throw_exception(args...), decltype(error));
    EXPECT_EQ(rep, rep_copy); // Throwing is const
  };
  // `result` flavor
  checkThrow(rich_error<RichErr>{});
  checkThrow(std::logic_error{"test"});
  // `Try` flavor
  try_rich_exception_ptr_private_t priv;
  checkThrow(rich_error<RichErr>{}, priv);
  checkThrow(std::logic_error{"test"}, priv);
}

TEST(RichExceptionPtrOwned, throwException) {
  checkThrowExceptionOwned<rich_exception_ptr>();
}
TEST(RichExceptionPtrOwned, throwExceptionSeparate) {
  checkThrowExceptionOwned<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtrOwned, throwExceptionPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkThrowExceptionOwned<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
