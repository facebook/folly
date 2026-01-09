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

#include <folly/result/test/rich_exception_ptr_common.h>

// Constexpr tests for `rich_exception_ptr` internal consistency checks.
// These are compile-time tests that verify size and bit invariants.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// This exists so that the test shows up in CI telemetry
TEST(RichExceptionPtrConstexprTest, allTestsAreCompileTime) {}

constexpr bool test_rich_exception_ptr_sizes() {
  static_assert(
      sizeof(detail::rich_exception_ptr_separate_storage) ==
      sizeof(rich_exception_ptr_separate));
  static_assert(
      sizeof(detail::rich_exception_ptr_packed_storage) ==
      sizeof(rich_exception_ptr_packed));
  static_assert(
      sizeof(rich_exception_ptr) ==
      (rich_exception_ptr_packed_storage::is_supported
           ? sizeof(detail::rich_exception_ptr_packed_storage)
           : sizeof(detail::rich_exception_ptr_separate_storage)));
  return true;
}

static_assert(test_rich_exception_ptr_sizes());

// These internal consistency checks for `rich_exception_ptr::bits_t` aim to
// prevent careless changes that could violate the invariants the code needs.
constexpr bool test_rich_exception_ptr_bits() {
  using R = detail::rich_exception_ptr_base_storage;

  // C++ doesn't let us modify the alignment bits of constexpr pointers.  We
  // need this to give `rich_exception_ptr_packed_storage` constexpr support
  // for the default ctor, and for immortal `rich_error`s.
  static_assert(R::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == 0);

  // "own" is mutually exclusive with all the special stuff
  static_assert(
      !(R::OWNS_EXCEPTION_PTR_and & R::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq));
  static_assert(!(R::OWNS_EXCEPTION_PTR_and & R::SMALL_VALUE_eq));
  static_assert(!(R::OWNS_EXCEPTION_PTR_and & R::SIGIL_eq));
  static_assert(
      !(R::OWNS_EXCEPTION_PTR_and & R::NOTHROW_OPERATION_CANCELLED_eq));

  // "owns" bits are meant to used with fast-path type markings
  static_assert(!(R::OWNS_EXCEPTION_PTR_and & R::mask_FAST_PATH_TYPES));
  // But the fast-path type markings are also compatible with the special
  // non-owned pointers.
  static_assert(
      R::IS_RICH_ERROR_BASE_masked_eq ==
      (R::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq & R::mask_FAST_PATH_TYPES));
  static_assert(
      R::IS_OPERATION_CANCELLED_masked_eq ==
      (R::NOTHROW_OPERATION_CANCELLED_eq & R::mask_FAST_PATH_TYPES));
  return true;
}
static_assert(test_rich_exception_ptr_bits());

// The UX of `rich_ptr_to_underlying_error` mostly matches a raw pointer, except
// it can print rich info when formatted, and some conversions are explicit.
//
// Runtime API use is amply covered in other tests -- this is just a smoke-test.
constexpr bool test_rich_ptr_to_underlying_error() {
  using Ptr = rich_ptr_to_underlying_error<RichErr>;
  static_assert(std::is_trivially_copyable_v<Ptr>);

  static_assert(std::is_constructible_v<Ptr, std::nullptr_t>);
  static_assert(!std::is_default_constructible_v<Ptr>);

  // Deliberately immovable for safety
  static_assert(!std::is_copy_constructible_v<Ptr>);
  static_assert(!std::is_copy_assignable_v<Ptr>);
  static_assert(!std::is_move_constructible_v<Ptr>);
  static_assert(!std::is_move_assignable_v<Ptr>);

  // Pointer-like semantics: dereference and arrow operators
  static_assert(std::is_same_v<decltype(*FOLLY_DECLVAL(Ptr)), RichErr&>);
  static_assert(
      std::is_same_v<decltype(FOLLY_DECLVAL(Ptr).operator->()), RichErr*>);

  // Explicitly convertible to bool
  static_assert(!std::is_convertible_v<Ptr, bool>);
  static_assert(std::is_constructible_v<bool, Ptr>);

  // Explicitly convertible to raw pointer
  static_assert(!std::is_convertible_v<Ptr, RichErr*>);
  static_assert(std::is_constructible_v<RichErr*, Ptr>);

  // Equality-comparable
  static_assert(std::equality_comparable<Ptr>);
  static_assert(requires(Ptr p) {
    { p == nullptr } -> std::same_as<bool>;
    { nullptr == p } -> std::same_as<bool>;
    { p != nullptr } -> std::same_as<bool>;
    { nullptr != p } -> std::same_as<bool>;
  });
  static_assert(requires(Ptr p, RichErr* r) {
    { p == r } -> std::same_as<bool>;
    { r == p } -> std::same_as<bool>;
    { p != r } -> std::same_as<bool>;
    { r != p } -> std::same_as<bool>;
  });

  return true;
}
static_assert(test_rich_ptr_to_underlying_error());

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
