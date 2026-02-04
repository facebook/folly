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
#include <folly/result/rich_error.h>
#include <folly/result/test/rich_exception_ptr_common.h>

#include <folly/portability/GTest.h>
#include <folly/result/immortal_rich_error.h>

// Tests for fundamental operations: copy, move, assignment, equality.
// `checkEptrRoundtrip` covers conversion to/from `std::exception_ptr`.

#if FOLLY_HAS_RESULT

namespace folly::test {

using namespace folly::detail;
using S = private_rich_exception_ptr_sigil;

// Returns `REP` after a round-trip through `std::exception_ptr`.
//
// Covers `from_exception_ptr_slow`, and `to_exception_ptr_slow`, both `const&`
// and `&&` qualified, both `Try` and `result` flavors:
//  - by a round-tripping through `std::exception_ptr`,
//  - by checking some core invariants in `checkEptrRoundtrip`.
template <typename REP>
REP checkEptrRoundtrip(const auto& original_src) {
  // Non-aliasing check -- bugs in REP could cause mutations in copies to
  // affect `original_src`.  To detect that, serialize some of its state.
  //
  // We don't serialize the raw pointers from  any `get_exception<>()` query,
  // since for immortals these pointers will differ after a round-trip.
  //
  // Future: It'd be nice to format `get_rich_error()` and
  // `get_exception<std:exception>()`, but there's currently no rendering
  // modifier to omit epitaphs, whereas this comparison should be
  // transparent to epitaphs.  Also format the REP itself, when available.
  auto serialize = [](const auto& rep) {
    auto rex = get_rich_error(rep).get();
    return std::tuple{
        rex ? fmt::format("{}", rex->all_codes_for_fmt()) : std::string{},
        rex ? rex->partial_message() : "",
        rex ? rex->source_location().line() : source_location{}.line(),
        rep.to_exception_ptr_slow(),
        rep == REP{}};
  };
  auto original_serialized = serialize(original_src);
  try_rich_exception_ptr_private_t priv;
  { // Copy conversion
    REP src{original_src};
    auto copy = REP::from_exception_ptr_slow(src.to_exception_ptr_slow());
    EXPECT_EQ( // `result` flavor
        src, REP::from_exception_ptr_slow(src.to_exception_ptr_slow()));
    EXPECT_EQ( // `Try` flavor
        src, REP::from_exception_ptr_slow(src.to_exception_ptr_slow(priv)));
    EXPECT_EQ(original_src, src); // src unchanged after copy
    EXPECT_EQ(original_serialized, serialize(src));
  }

  auto checkMoveConversion = [&](auto... args) {
    REP src{original_src};
    auto dst = REP::from_exception_ptr_slow(
        std::move(src).to_exception_ptr_slow(args...));
    EXPECT_EQ(original_src, dst);
    EXPECT_EQ(rich_exception_ptr{}, src); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(original_serialized, serialize(dst));
  };
  checkMoveConversion(); // `result` flavor
  checkMoveConversion(priv); // `Try` flavor

  EXPECT_EQ(original_serialized, serialize(original_src));

  return REP::from_exception_ptr_slow(original_src.to_exception_ptr_slow());
}

template <typename REP>
auto allEpitaphRepVariants() {
  auto RichErr1 = REP{rich_error<RichErr>{}};
  auto logic_err = REP{std::runtime_error{"test"}};
  auto immortal = immortal_rich_error_t<REP, RichErr>{}.ptr();
  auto nothrow_oc = REP{StoppedNoThrow{}};
  // NOTE: Two strings are equal iff the REPs should be equal.
  return std::vector<std::pair<std::string, REP>>{
      {"owned_richerr1", RichErr1},
      {"owned_richerr1", checkEptrRoundtrip<REP>(RichErr1)},

      // A new owned instance has a distinct exception object pointer.
      {"owned_richerr2", REP{rich_error<RichErr>{}}},

      // Owned non-rich errors don't behave any differently.
      {"owned_logic_error", logic_err},
      {"owned_logic_error", checkEptrRoundtrip<REP>(logic_err)},

      {"immortal_richerr", immortal},
      // All immortals with the same args have the same object pointer.
      {"immortal_richerr", immortal_rich_error_t<REP, RichErr>{}.ptr()},
      // Round-tripping an immortal makes an "owned" pointer to its mutable
      // singleton. BUT -- the comparison is smart and makes it compare equal.
      {"immortal_richerr", checkEptrRoundtrip<REP>(immortal)},

      {"nothrow_oc", nothrow_oc},
      // This is an "owned" eptr, but it is logically equal to nothrow OC
      {"nothrow_oc", checkEptrRoundtrip<REP>(nothrow_oc)},
  };
}

// Helps test copy/move construction, assignment, and equality.
// Indirectly (via `checkEptrRoundtrip`) tests `std::exception_ptr` conversion.
template <typename REP>
auto allRepVariants() {
  auto reps = allEpitaphRepVariants<REP>();
  // Add epitaphs to the whole test matrix: for each base variant, add
  // versions with epitaph wrappers. They wrap an underlying error with
  // additional context. The inner type must be `rich_exception_ptr` as
  // required by `underlying_error()`.
  if constexpr (std::is_same_v<rich_exception_ptr, REP>) {
    auto repsToAddEpitaphs = reps;
    for (auto& [key, rep] : repsToAddEpitaphs) {
      auto withEpitaphs = REP{
          rich_error<epitaph_non_value>{std::move(rep), rich_msg{"epitaph"}}};
      // Same key, since epitaphs are transparent
      reps.emplace_back(key, withEpitaphs);
      reps.emplace_back(key, checkEptrRoundtrip<REP>(withEpitaphs));
    }
  } else { // rich_exception_ptr != REP
    auto repsToAddEpitaphs = allEpitaphRepVariants<rich_exception_ptr>();
    for (auto& [key, rep] : repsToAddEpitaphs) {
      auto withEpitaphs = REP{
          rich_error<epitaph_non_value>{std::move(rep), rich_msg{"epitaph"}}};
      // Different key -- although epitaphs are transparent, the different
      // types `rich_exception_ptr != REP` guarantee these won't compare equal.
      reps.emplace_back("epitaph_" + key, withEpitaphs);
      reps.emplace_back(
          "epitaph_" + key, checkEptrRoundtrip<REP>(withEpitaphs));
    }
  }
  // Avoid adding epitaphs to empty-eptr -- `epitaph_non_value` currently has
  // a debug-assert against this usage.  If we change our mind, then move
  // empty-eptr into `allEpitaphRepVariants` to add coverage.
  {
    auto empty = REP{};
    reps.emplace_back("empty", empty);
    // NB: This looks "owned", but ... it is not currently possible to make
    // an "owned" REP with an empty `std::exception_ptr`. This ctor
    // explicitly maps `eptr{}` to be the same bit-state as `REP{}`.
    reps.emplace_back(
        "empty", REP::from_exception_ptr_slow(std::exception_ptr{}));
    reps.emplace_back("empty", checkEptrRoundtrip<REP>(empty));
  }
  // Avoid `checkEptrRoundtrip` on sigils.  By design, they don't support
  // `to_exception_ptr_slow`.  We could test adding epitaphs to sigils (as
  // in the loops above), but that should never be used, so omit it.
  reps.emplace_back("empty_try", REP{vtag<S::EMPTY_TRY>});
  reps.emplace_back("result_has_value", REP{vtag<S::RESULT_HAS_VALUE>});
  return reps;
}

template <typename REP>
void checkAllComparisons() {
  const auto reps = allRepVariants<REP>();
  for (size_t i = 0; i < reps.size(); ++i) {
    for (size_t j = 0; j < reps.size(); ++j) {
      const auto& [lname, lhs] = reps[i];
      const auto& [rname, rhs] = reps[j];
      if (lname == rname) {
        EXPECT_EQ(lhs, rhs) << lname << " vs " << rname;
      } else {
        EXPECT_NE(lhs, rhs) << lname << " vs " << rname;
      }
    }
  }
}

// Compare each `rich_exception_ptr` against every other
TEST(RichExceptionPtr, allComparisons) {
  checkAllComparisons<rich_exception_ptr>();
}
TEST(RichExceptionPtr, allComparisonsSeparate) {
  checkAllComparisons<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtr, allComparisonsPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkAllComparisons<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkAllAssignments() {
  const auto reps = allRepVariants<REP>();
  for (size_t i = 0; i < reps.size(); ++i) {
    for (size_t j = 0; j < reps.size(); ++j) {
      const auto& [unused1, original_src] = reps[i];
      const auto& [unused2, original_dst] = reps[j];
      // Moved-from is empty, except for sigils (unowned pointer-sized states).
      auto checkMovedOut = [&](auto& rep) {
        if (REP{vtag<S::EMPTY_TRY>} == original_src ||
            REP{vtag<S::RESULT_HAS_VALUE>} == original_src) {
          EXPECT_EQ(original_src, rep);
        } else {
          EXPECT_EQ(rich_exception_ptr{}, rep);
        }
      };
      { // Copy assignment
        auto src{original_src};
        auto dst{original_dst};

        dst = src;
        EXPECT_EQ(dst, src);
        EXPECT_EQ(src, original_src); // src unchanged after copy

        dst = static_cast<const REP&>(dst); // suppress self-assign warning
        EXPECT_EQ(dst, original_src); // self-assignment is a no-op
      }
      { // Move assignment
        auto src{original_src};
        auto dst{original_dst};

        dst = std::move(src);
        EXPECT_EQ(dst, original_src);
        checkMovedOut(src); // NOLINT(bugprone-use-after-move)

        dst = static_cast<REP&&>(dst); // suppress self-assign warning
        EXPECT_EQ(dst, original_src); // self-assignment is a no-op
      }
      { // Move construction (copy is exercised throughout)
        auto src{original_src};
        auto dst{std::move(src)};
        EXPECT_EQ(dst, original_src);
        checkMovedOut(src); // NOLINT(bugprone-use-after-move)
      }
    }
  }
}

// Assign each `rich_exception_ptr` variant on top of every other
TEST(RichExceptionPtr, allAssignments) {
  checkAllAssignments<rich_exception_ptr>();
}
TEST(RichExceptionPtr, allAssignmentsSeparate) {
  checkAllAssignments<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtr, allAssignmentsPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkAllAssignments<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
