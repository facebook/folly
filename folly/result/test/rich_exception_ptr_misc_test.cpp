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

#include <folly/OperationCancelled.h>
#include <folly/portability/GTest.h>

// Covers "special case" states: nothrow cancellation, empty `Try`, empty eptr.
//
// Future: Add tests for the "small value" state, once it has a ctor. For
// more details, see `docs/future_small_value.md`.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// IMPORTANT: There are more `#if 0` tests below. Run them all!
constexpr bool manualTestsForStaticAsserts() {
#if 0 // Must use `rich_error` or `immortal_rich_error`
  (void)rich_exception_ptr{RichErr{}};
#endif
  return true;
}
static_assert(manualTestsForStaticAsserts());

// Lightly test copy/move ctor, copy/move assignment, and equality.
// `rich_exception_ptr_fundamentals_test.cpp` covers these more exhaustively.
template <typename REP>
void checkFundamentals(const REP& rep, const REP& other) {
  EXPECT_EQ(rep, rep);
  EXPECT_NE(rep, other);
  { // Copy ctor & assignment
    EXPECT_EQ(rep, REP{rep});
    REP rep2;
    rep2 = rep;
    EXPECT_EQ(rep, rep2);
  }
  { // Move ctor &assignment
    EXPECT_EQ(rep, REP{REP{rep}});
    REP rep2;
    rep2 = REP{rep};
    EXPECT_EQ(rep, rep2);
  }
}

template <typename REP>
void checkEmptyTryToExceptionPtr(REP& rep) {
  // For `result`, avoid throwing: debug-fatal, returning an eptr in opt.
  if (kIsDebug) {
    auto re = "Cannot use `to_exception_ptr_slow` in value or empty `Try` ";
    EXPECT_DEATH({ (void)std::as_const(rep).to_exception_ptr_slow(); }, re);
    EXPECT_DEATH({ (void)REP{rep}.to_exception_ptr_slow(); }, re);
  } else {
    auto eptr1 = std::as_const(rep).to_exception_ptr_slow();
    EXPECT_TRUE(get_exception<bad_result_access_error>(eptr1));
    auto eptr2 = REP{rep}.to_exception_ptr_slow();
    EXPECT_TRUE(get_exception<bad_result_access_error>(eptr2));
  }

  // For `Try`, alway throw
  try_rich_exception_ptr_private_t priv;
  EXPECT_THROW(
      (void)std::as_const(rep).to_exception_ptr_slow(priv),
      StubUsingUninitializedTry);
  EXPECT_THROW(
      (void)REP{rep}.to_exception_ptr_slow(priv), StubUsingUninitializedTry);
}

template <typename REP>
void checkEmptyTry() {
  REP rep{make_empty_try_t{}};
  checkFundamentals(rep, REP{});

  if (kIsDebug) { // For `result`, debug-fatal, with fallback to throwing
    EXPECT_DEATH(
        { rep.throw_exception(); }, "Cannot `throw_exception` on empty `Try`");
  } else {
    EXPECT_THROW(rep.throw_exception(), empty_result_error);
  }
  EXPECT_THROW( // For `Try`, always throws
      rep.throw_exception(try_rich_exception_ptr_private_t{}),
      detail::StubUsingUninitializedTry);

  checkEmptyTryToExceptionPtr<REP>(rep); // Covers both `result` and `Try`

  // We throw `UninitializedTry` / `empty_result_error`, but cannot query them
  checkGetExceptionBoth<
      GetExceptionResult{.isHit = false},
      std::exception,
      rich_error_base,
      StubUsingUninitializedTry,
      empty_result_error>(rep);
}

TEST(RichExceptionPtr, emptyTry) {
  checkEmptyTry<rich_exception_ptr>();
}
TEST(RichExceptionPtr, emptyTrySeparate) {
  checkEmptyTry<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtr, emptyTryPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkEmptyTry<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkNothrowOperationCancelled() {
  auto checkToEptr = [](auto&& rep, auto... priv) {
    auto eptr = static_cast<decltype(rep)>(rep).to_exception_ptr_slow(priv...);
    // This one should later be banned by a `static_assert`...
    EXPECT_TRUE(get_exception<StubNothrowOperationCancelled>(eptr));
    // Future: The non-stub version will inherit from OC, which will NOT
    // inherit from `std::exception`.
    // EXPECT_TRUE(get_exception<OperationCancelled>(eptr));
    // EXPECT_FALSE(get_exception<std::exception>(eptr));
  };

  REP rep{StubNothrowOperationCancelled{}};
  checkFundamentals(rep, REP{});

  // `throw_exception`: `result` and `Try` flavors
  // Future: should probably actually catch `OperationCancelled` here
  EXPECT_THROW(rep.throw_exception(), StubNothrowOperationCancelled);
  EXPECT_THROW(
      rep.throw_exception(try_rich_exception_ptr_private_t{}),
      StubNothrowOperationCancelled);

  // `to_exception_ptr_slow`: `const&` / `&&` with `result` / `Try`
  checkToEptr(std::as_const(rep));
  checkToEptr(REP{rep});
  checkToEptr(std::as_const(rep), try_rich_exception_ptr_private_t{});
  checkToEptr(REP{rep}, try_rich_exception_ptr_private_t{});

  checkGetExceptionBoth<
      GetExceptionResult{.isHit = false},
      std::exception,
      RichErr>(rep);

  // There's a `static_assert` against querying `NothrowOperationCancelled`
#if 0 // manual test
  checkGetOuterException<
      GetExceptionResult{.isHit = false},
      StubNothrowOperationCancelled>(rep);
#elif 0
  checkGetException<
      GetExceptionResult{.isHit = false},
      StubNothrowOperationCancelled>(rep);
#endif

  // But, we *can* query for `OperationCancelled`
  EXPECT_TRUE(get_exception<OperationCancelled>(std::as_const(rep)));
  EXPECT_TRUE(get_mutable_exception<OperationCancelled>(rep));
  EXPECT_TRUE(
      std::as_const(rep).template get_outer_exception<OperationCancelled>());
  EXPECT_TRUE(rep.template get_outer_exception<OperationCancelled>());
}

TEST(RichExceptionPtr, nothrowOperationCancelled) {
  checkNothrowOperationCancelled<rich_exception_ptr>();
}
TEST(RichExceptionPtr, nothrowOperationCancelledSeparate) {
  checkNothrowOperationCancelled<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtr, nothrowOperationCancelledPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkNothrowOperationCancelled<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkEmptyEptr() {
  try_rich_exception_ptr_private_t priv;
  REP rep;
  checkFundamentals(rep, REP{rich_error<RichErr>{}});

  // `throw_exception` always terminates on empty eptr (rethrowing empty is UB)
  auto re = "Cannot use `throw_exception\\(\\)` with an empty or invalid ";
  EXPECT_DEATH({ rep.throw_exception(); }, re);
  EXPECT_DEATH({ rep.throw_exception(priv); }, re);

  // `to_exception_ptr_slow` returns empty eptr
  EXPECT_FALSE(std::as_const(rep).to_exception_ptr_slow());
  EXPECT_FALSE(REP{rep}.to_exception_ptr_slow());
  EXPECT_FALSE(std::as_const(rep).to_exception_ptr_slow(priv));
  EXPECT_FALSE(REP{rep}.to_exception_ptr_slow(priv));

  // Cannot get any exceptions from empty eptr
  checkGetExceptionBoth<
      GetExceptionResult{.isHit = false},
      std::exception,
      rich_error_base,
      OperationCancelled>(rep);
}

TEST(RichExceptionPtr, emptyEptr) {
  checkEmptyEptr<rich_exception_ptr>();
}
TEST(RichExceptionPtr, emptyEptrSeparate) {
  checkEmptyEptr<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtr, emptyEptrPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkEmptyEptr<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
