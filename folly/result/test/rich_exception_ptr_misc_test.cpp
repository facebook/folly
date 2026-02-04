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

namespace folly::test {

using namespace folly::detail;
using S = private_rich_exception_ptr_sigil;

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

// Tests `to_exception_ptr_slow` for sigil states.
//  - `result` path: debug-fatal, returning bad_result_access_error eptr in opt.
//  - `Try` path: throws StubUsingUninitializedTry; if `tryDebugDeathRe` is set,
//    expects a debug death first (for unexpected/pathological sigils like
//    RESULT_HAS_VALUE appearing in a Try context).
template <typename REP>
void checkSigilToExceptionPtr(REP& rep, const char* tryDebugDeathRe = nullptr) {
  // For `result`, avoid throwing: debug-fatal, returning an eptr in opt.
  if (kIsDebug) {
    auto re = "Cannot use `to_exception_ptr_slow` in sigil or small-value";
    EXPECT_DEATH({ (void)std::as_const(rep).to_exception_ptr_slow(); }, re);
    EXPECT_DEATH({ (void)REP{rep}.to_exception_ptr_slow(); }, re);
  } else {
    auto eptr1 = std::as_const(rep).to_exception_ptr_slow();
    EXPECT_TRUE(get_exception<bad_result_access_error>(eptr1));
    auto eptr2 = REP{rep}.to_exception_ptr_slow();
    EXPECT_TRUE(get_exception<bad_result_access_error>(eptr2));
  }

  // For `Try`, always throws StubUsingUninitializedTry.
  try_rich_exception_ptr_private_t priv;
  if (tryDebugDeathRe && kIsDebug) {
    // Pathological sigil for Try -- debug-asserts before throwing.
    EXPECT_DEATH(
        (void)std::as_const(rep).to_exception_ptr_slow(priv), tryDebugDeathRe);
    EXPECT_DEATH((void)REP{rep}.to_exception_ptr_slow(priv), tryDebugDeathRe);
  } else {
    EXPECT_THROW(
        (void)std::as_const(rep).to_exception_ptr_slow(priv),
        StubUsingUninitializedTry);
    EXPECT_THROW(
        (void)REP{rep}.to_exception_ptr_slow(priv), StubUsingUninitializedTry);
  }
}

// Tests a sigil state: fundamentals, throw_exception, to_exception_ptr_slow,
// get_exception.  If `tryDebugDeathRe` is set, expects debug deaths on the
// Try path (for pathological sigils like RESULT_HAS_VALUE in a Try context).
template <typename REP, S Sigil>
void checkSigil(const char* tryDebugDeathRe = nullptr) {
  constexpr auto otherSigil =
      (Sigil == S::EMPTY_TRY) ? S::RESULT_HAS_VALUE : S::EMPTY_TRY;

  REP rep{vtag<Sigil>};
  checkFundamentals(rep, REP{});

  // Check distinctness from other sigil + `has_sigil<>` queries
  EXPECT_NE(rep, REP{vtag<otherSigil>});
  EXPECT_TRUE(rep.template has_sigil<Sigil>());
  EXPECT_FALSE(rep.template has_sigil<otherSigil>());

  // throw_exception: `result` path is debug-fatal for both sigils
  if (kIsDebug) {
    EXPECT_DEATH(
        { rep.throw_exception(); }, "Cannot `throw_exception` in sigil state");
  } else {
    EXPECT_THROW(rep.throw_exception(), bad_result_access_error);
  }
  // throw_exception: `Try` path always throws StubUsingUninitializedTry.
  // If `tryDebugDeathRe` is set, the sigil is unexpected for Try, so we
  // also expect a debug death before the throw.
  if (tryDebugDeathRe && kIsDebug) {
    EXPECT_DEATH(
        rep.throw_exception(try_rich_exception_ptr_private_t{}),
        tryDebugDeathRe);
  } else {
    EXPECT_THROW(
        rep.throw_exception(try_rich_exception_ptr_private_t{}),
        StubUsingUninitializedTry);
  }

  checkSigilToExceptionPtr<REP>(rep, tryDebugDeathRe);

  // We throw `UsingUninitializedTry` / `bad_result_access_error`, but can't
  // query them -- the sigil itself doesn't store an exception.
  checkGetExceptionBoth<
      GetExceptionResult{.isHit = false},
      std::exception,
      rich_error_base,
      StubUsingUninitializedTry,
      bad_result_access_error,
      OperationCancelled>(rep);
}

TEST(RichExceptionPtr, emptyTry) {
  checkSigil<rich_exception_ptr, S::EMPTY_TRY>();
}
TEST(RichExceptionPtr, emptyTrySeparate) {
  checkSigil<rich_exception_ptr_separate, S::EMPTY_TRY>();
}
TEST(RichExceptionPtr, emptyTryPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkSigil<rich_exception_ptr_packed, S::EMPTY_TRY>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

TEST(RichExceptionPtr, resultHasValue) {
  checkSigil<rich_exception_ptr, S::RESULT_HAS_VALUE>(
      "Try has unexpected sigil");
}
TEST(RichExceptionPtr, resultHasValueSeparate) {
  checkSigil<rich_exception_ptr_separate, S::RESULT_HAS_VALUE>(
      "Try has unexpected sigil");
}
TEST(RichExceptionPtr, resultHasValuePacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkSigil<rich_exception_ptr_packed, S::RESULT_HAS_VALUE>(
        "Try has unexpected sigil");
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkNothrowOperationCancelled() {
  auto checkToEptr = [](auto&& rep, auto... priv) {
    auto eptr = static_cast<decltype(rep)>(rep).to_exception_ptr_slow(priv...);
    // This one should later be banned by a `static_assert`...
    EXPECT_TRUE(get_exception<StoppedNoThrow>(eptr));
    // Future: The non-stub version will inherit from OC, which will NOT
    // inherit from `std::exception`.
    // EXPECT_TRUE(get_exception<OperationCancelled>(eptr));
    // EXPECT_FALSE(get_exception<std::exception>(eptr));
  };

  REP rep{StoppedNoThrow{}};
  checkFundamentals(rep, REP{});

  // `throw_exception`: `result` and `Try` flavors
  // Future: should probably actually catch `OperationCancelled` here
  EXPECT_THROW(rep.throw_exception(), StoppedNoThrow);
  EXPECT_THROW(
      rep.throw_exception(try_rich_exception_ptr_private_t{}), StoppedNoThrow);

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
      StoppedNoThrow>(rep);
#elif 0
  checkGetException<GetExceptionResult{.isHit = false}, StoppedNoThrow>(rep);
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

  // Empty eptr is not a sigil
  EXPECT_FALSE(rep.template has_sigil<S::EMPTY_TRY>());
  EXPECT_FALSE(rep.template has_sigil<S::RESULT_HAS_VALUE>());

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

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
