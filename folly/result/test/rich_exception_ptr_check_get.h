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

#pragma once

#include <folly/result/enrich_non_value.h>
#include <folly/result/rich_error.h>
#include <folly/result/test/rich_exception_ptr_common.h>

// Utilities for testing `get_exception` / `get_outer_exception` queries on
// `rich_exception_ptr`.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// Describes the `rich_ptr` returned from `get_...exception`.
struct GetExceptionResult {
  // Got a non-null pointer of the queried type.
  bool isHit{false};
  // Only applies for `get_exception` -- with `get_outer_exception`, there is no
  // `top_rich_error_` to test.
  //
  // Only applies to hits -- per the `rich_exception_ptr::get_exception` doc,
  // there is no contract about `top_rich_error_` for misses, and the formatter
  // doesn't look for it.
  //
  // Since we currently don't need provenance for null `rich_ptr...`s, the
  // implemented behavior of `top_rich_error_` is inconsistent (for perf
  // reasons) -- we populate it in all cases except "querying non-base rich
  // error from an immortal".
  bool hitHasTopRichError{false};
  // When we query immortals, the mutable & immutable object pointers differ.
  bool constAndMutPointersAreSame{true};
};

// Check exception query hit/miss, and top_rich_error_ status if available.
template <auto ExpectedResult, typename Ex>
void expectGetExceptionResult(
    const rich_ptr_to_underlying_error<Ex>& rich_ptr) {
  // Not a constraint due to `rich_error_base.h` friendship
  static_assert(std::is_same_v<GetExceptionResult, decltype(ExpectedResult)>);
  EXPECT_EQ(ExpectedResult.isHit, bool{rich_ptr});
  if (ExpectedResult.isHit) {
    EXPECT_EQ(
        ExpectedResult.hitHasTopRichError, rich_ptr.top_rich_error_ != nullptr);
  }
}
template <GetExceptionResult Expected, typename Ex>
void expectGetExceptionResult(const Ex* raw_ptr) {
  // `get_outer_exception` returns a raw pointer, cannot have `top_rich_error_`
  static_assert(!Expected.hitHasTopRichError);
  EXPECT_EQ(Expected.isHit, raw_ptr != nullptr);
}

// Queried exception pointers match, except for immortals
void checkConstAndMutPointers(
    GetExceptionResult expected, auto constPtr, auto mutPtr) {
  if (!expected.isHit || expected.constAndMutPointersAreSame) {
    EXPECT_EQ(constPtr, mutPtr);
  } else {
    EXPECT_NE(constPtr, mutPtr);
  }
}

// Helpers for testing exception access:
//   checkGetExceptionBoth<GetExceptionResult{.isHit = true}, Ex1, Ex2>(rep)
//   checkGetException<GetExceptionResult{.isHit = false}, Ex>(rep)
template <GetExceptionResult Expected, typename... Exs>
void checkGetOuterException(auto& rep) {
  (
      [&]() {
        auto constPtr = std::as_const(rep).template get_outer_exception<Exs>();
        auto mutPtr = rep.template get_outer_exception<Exs>();
        checkConstAndMutPointers(Expected, constPtr, mutPtr);
        expectGetExceptionResult<Expected>(constPtr);
        expectGetExceptionResult<Expected>(mutPtr);
      }(),
      ...);
}
template <GetExceptionResult Expected, typename... Exs>
void checkGetException(auto& rep) {
  (
      [&]() {
        auto constPtr = get_exception<Exs>(std::as_const(rep));
        auto mutPtr = get_mutable_exception<Exs>(rep);
        checkConstAndMutPointers(Expected, constPtr.get(), mutPtr.get());
        expectGetExceptionResult<Expected>(constPtr);
        expectGetExceptionResult<Expected>(mutPtr);
      }(),
      ...);
}
template <GetExceptionResult Expected, typename... Exs>
void checkGetExceptionBoth(auto& rep) {
  checkGetException<Expected, Exs...>(rep);
  checkGetOuterException<
      // `get_outer_exception` returns raw pointers, so no `top_rich_error_`
      GetExceptionResult{
          .isHit = Expected.isHit,
          .constAndMutPointersAreSame = Expected.constAndMutPointersAreSame},
      Exs...>(rep);
}

// Reuse the same `rich_error<RichErr>` tests for immortal & owned REPs
template <bool PointersAreSame = true>
void checkGetExceptionForRichErr(auto rep) {
  checkGetExceptionBoth<
      GetExceptionResult{
          .isHit = false, .constAndMutPointersAreSame = PointersAreSame},
      std::logic_error,
      std::runtime_error>(rep);
  checkGetExceptionBoth<
      GetExceptionResult{
          .isHit = true,
          .hitHasTopRichError = true,
          .constAndMutPointersAreSame = PointersAreSame},
      rich_error_base,
      std::exception,
      RichErr,
      rich_error<RichErr>>(rep);
}

// Reuse the same "rich error with enrichment wrapper" tests for immortals &
// owned pointers.
//
// Note that only the enrichment wrapper is the template parameter `REP`. The
// inner type is `rich_exception_ptr`, as required by `underlying_error()`.
template <typename REP, bool PointersAreSame = true>
void checkGetExceptionForEnrichedRichErr(rich_exception_ptr rep) {
  REP rep_wrapped{
      rich_error<detail::enriched_non_value>{copy(rep), rich_msg{"msg"}}};
  checkGetException<
      GetExceptionResult{.isHit = false},
      std::logic_error,
      // Enrichment is transparent, so these 2 miss
      detail::enriched_non_value,
      rich_error<detail::enriched_non_value>>(rep_wrapped);
  // Querying the underlying error should hit, with `top_rich_error_` set
  checkGetException<
      GetExceptionResult{
          .isHit = true,
          .hitHasTopRichError = true,
          .constAndMutPointersAreSame = PointersAreSame},
      std::exception,
      RichErr,
      rich_error<RichErr>>(rep_wrapped);
  // `get_outer_exception` sees enrichment wrapper, not the underlying error
  checkGetOuterException<
      GetExceptionResult{.isHit = false},
      RichErr,
      rich_error<RichErr>>(rep_wrapped);
  // `PointersAreSame` does not apply here, since we're accessing an "owned"
  // outer error, not the immortal.
  checkGetOuterException<
      GetExceptionResult{.isHit = true},
      std::exception,
      rich_error_base,
      detail::enriched_non_value,
      rich_error<detail::enriched_non_value>>(rep_wrapped);
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
