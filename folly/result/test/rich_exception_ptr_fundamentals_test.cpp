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

#include <folly/result/rich_error.h>
#include <folly/result/test/rich_exception_ptr_common.h>

#include <folly/portability/GTest.h>
#include <folly/result/immortal_rich_error.h>

// Tests for fundamental operations: copy, move, assignment, equality.
// `checkEptrRoundtrip` covers conversion to/from `std::exception_ptr`.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// Returns `REP` after a round-trip through `std::exception_ptr`.
//
// Covers `from_exception_ptr_slow`, and `to_exception_ptr_slow`, both `const&`
// and `&&` qualified, both `Try` and `result` flavors:
//  - by a round-tripping through `std::exception_ptr`,
//  - by checking some core invariants in `checkEptrRoundtrip`.
template <typename REP>
REP checkEptrRoundtrip(const auto& original_src) {
  try_rich_exception_ptr_private_t priv;
  { // Copy conversion
    REP src{original_src};
    auto copy = REP::from_exception_ptr_slow(src.to_exception_ptr_slow());
    EXPECT_EQ( // `result` flavor
        src, REP::from_exception_ptr_slow(src.to_exception_ptr_slow()));
    EXPECT_EQ( // `Try` flavor
        src, REP::from_exception_ptr_slow(src.to_exception_ptr_slow(priv)));
    EXPECT_EQ(original_src, src); // src unchanged after copy
  }

  auto checkMoveConversion = [&](auto... args) {
    REP src{original_src};
    auto dst = REP::from_exception_ptr_slow(
        std::move(src).to_exception_ptr_slow(args...));
    EXPECT_EQ(original_src, dst);
    EXPECT_EQ(rich_exception_ptr{}, src); // NOLINT(bugprone-use-after-move)
  };
  checkMoveConversion(); // `result` flavor
  checkMoveConversion(priv); // `Try` flavor

  return REP::from_exception_ptr_slow(original_src.to_exception_ptr_slow());
}

// Helps test copy/move construction, assignment, and equality.
// Indirectly (via `checkEptrRoundtrip`) tests `std::exception_ptr` conversion.
template <typename REP>
auto allRepVariants() {
  auto empty = REP{};
  auto RichErr1 = REP{rich_error<RichErr>{}};
  auto logic_err = REP{std::runtime_error{"test"}};
  auto immortal = immortal_rich_error_t<REP, RichErr>{}.ptr();
  auto nothrow_oc = REP{StubNothrowOperationCancelled{}};
  // NOTE: Two strings are equal iff the REPs should be equal.
  return std::vector<std::pair<std::string, REP>>{
      {"empty_try", REP{make_empty_try_t{}}},

      {"empty", empty},
      // NB: This looks "owned", but ... it is not currently possible to make
      // an "owned" REP with an empty `std::exception_ptr`. This ctor
      // explicitly maps `eptr{}` to be the same bit-state as `REP{}`.
      {"empty", REP::from_exception_ptr_slow(std::exception_ptr{})},
      {"empty", checkEptrRoundtrip<REP>(empty)},

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
      {"nothrow_oc", checkEptrRoundtrip<REP>(nothrow_oc)}};
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
      const auto& [_, original_src] = reps[i];
      const auto& [__, original_dst] = reps[j];
      // Moved-from is empty, except for "empty try".
      auto checkMovedOut = [&](auto& rep) {
        if (REP{make_empty_try_t{}} == original_src) {
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

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
