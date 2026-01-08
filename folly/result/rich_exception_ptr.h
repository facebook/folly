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

#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/lang/Exception.h>
#include <folly/result/detail/rich_exception_ptr_storage.h>
#include <folly/result/rich_error_base.h>

#if FOLLY_HAS_RESULT

namespace folly {

struct OperationCancelled;

template <typename, typename, auto...>
class immortal_rich_error_t;

namespace detail {

// The "STUB_" prefix goes away when we integrate with `result.h`.
// These are in `detail` since they're opt-build fallbacks for a debug-fatal.
struct STUB_bad_result_access_error : public std::exception {};
struct STUB_empty_result_error : public std::exception {};

// Stub types that will be replaced by integrations with other folly/ types.
//
// Will be replaced by real types from OperationCancelled.h, which will both
// derive from `OperationCancelled`.
struct StubNothrowOperationCancelled {}; // NOT an `std::exception`
struct StubThrownOperationCancelled : std::exception {};
// Will be replaced by the real types that are currently in Try.h
struct StubUsingUninitializedTry : std::exception {};
struct StubTryException : std::exception {};

template <typename T>
using detect_folly_detail_base_of_rich_error =
    typename T::folly_detail_base_of_rich_error;

// Tag types for non-public `rich_exception_ptr` interfaces
struct force_slow_rtti_t {};
struct make_empty_try_t {};
struct try_rich_exception_ptr_private_t {};

template <typename Derived, typename B>
// `private` inheritance to show that the members of the storage classes are
// all implementation details -- all the public interfaces are here.
class rich_exception_ptr_impl : private B {
 private:
  // This allows comparing REPs with different storage.  Only tests need it,
  // since they explicitly cover both "separate" and "packed" storage, whereas
  // `underlying_ptr()` always uses the default storage.
  template <typename, typename>
  friend class rich_exception_ptr_impl;

  using typename B::bits_t;

  void set_empty_try() {
    B::apply_bits_after_setting_data_with(
        [](auto& d) { d.uintptr_ = B::kSigilEmptyTry; }, B::SIGIL_eq);
  }

  constexpr const detail::immortal_exception_storage* get_immortal_storage()
      const {
    if (!std::is_constant_evaluated()) {
      B::debug_assert("get_immortal_storage", !B::get_bits());
    }
    return B::get_immortal_storage_or_punned_uintptr();
  }

  constexpr void set_immortal_storage_and_bits(
      const detail::immortal_exception_storage* p, typename B::bits_t bits) {
    B::apply_bits_after_setting_data_with(
        [p](auto& d) { d.immortal_storage_ = p; }, bits);
  }

  // Future: Could implement this overload via `folly::copy()`, if you find
  // that `extract_exception_ptr` always optimizes away.
  void assign_owned_eptr_and_bits(
      const std::exception_ptr& ep, typename B::bits_t bits) {
    B::apply_bits_after_setting_data_with(
        [&ep](auto& d) {
          new (&d.eptr_ref_guard_.ref()) std::exception_ptr{ep};
        },
        bits);
  }

  void assign_owned_eptr_and_bits(
      std::exception_ptr&& ep, typename B::bits_t bits) {
    B::apply_bits_after_setting_data_with(
        [&ep](auto& d) {
          new (&d.eptr_ref_guard_.ref())
              std::exception_ptr{detail::extract_exception_ptr(std::move(ep))};
        },
        bits);
  }

  // Pre-condition: `rich_exception_ptr` is in any valid state.
  //
  // Post-condition: Any stored owned object (i.e. eptr) is destructed, and
  // all member vars of the object are in an undefined state.  To reuse it,
  // they must be re-set as-if constructing it from scratch.
  constexpr void destroy_owned_state_and_bits_caller_must_reset() {
    if (B::OWNS_EXCEPTION_PTR_and & B::get_bits()) {
      B::get_eptr_ref_guard().ref().~exception_ptr();
    }
  }

  // IMPORTANT: Must not read `this`, it may be after-dtor, or before-ctor
  constexpr void copy_unowned_pointer_sized_state(
      const rich_exception_ptr_impl& other) {
    if (!std::is_constant_evaluated()) {
      // NB: Technically, this works for !B::bits_store_eptr(B::get_bits())),
      // but we expect to use it in a subset of those cases.
      B::debug_assert(
          "copy_unowned_pointer_sized_state",
          B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == other.get_bits() ||
              B::SIGIL_eq == other.get_bits() ||
              // We use this code path to copy / move small values uintptrs,
              // the copy/move ctors must be called by whatever contains this.
              B::SMALL_VALUE_eq == other.get_bits());
    }
    // Here, we might have an immortal error, or a sigil, a small value, etc.
    // All of these require copying "bits + uintptr".  One way to achieve this:
    //   set_immortal_storage_and_bits(
    //       other.get_immortal_storage_or_punned_uintptr(), other.get_bits());
    // However, that debug-asserts the first arg has clean low bits, which
    // would only be true for immortals, and not for the other cases.  Rather
    // than conditionally relax the assertion, or add other branches, let's
    // just copy all bits of the storage state.  This is a bit wasteful on
    // Windows, where eptr is 2 pointers, but it's the simplest code for now.
    //
    // Copy bit state storage, bypassing `rich_exception_ptr_impl::operator=`
    B::operator=(other);
  }

  // IMPORTANT: Must not read `this`, it may be after-dtor, or before-ctor
  constexpr void copy_from_impl(const rich_exception_ptr_impl& other) {
    if (B::OWNS_EXCEPTION_PTR_and & other.get_bits()) {
      assign_owned_eptr_and_bits(
          other.get_eptr_ref_guard().ref(), other.get_bits());
    } else if (B::NOTHROW_OPERATION_CANCELLED_eq == other.get_bits()) {
      // Copy bit state storage, bypassing `rich_exception_ptr_impl::operator=`.
      // DO NOT run the `std::exception_ptr` copy ctor; immortal OC is unowned.
      B::operator=(other);
    } else {
      // Immortal RE, empty eptr, empty Try, small value uintptr
      copy_unowned_pointer_sized_state(other);
    }
  }

  constexpr void make_moved_out(bits_t bits) {
    // When `rich_exception_ptr` has an exception, the right moved-out state is
    // "empty eptr".
    if ((B::OWNS_EXCEPTION_PTR_and & bits) ||
        B::NOTHROW_OPERATION_CANCELLED_eq == bits ||
        B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
      set_immortal_storage_and_bits(
          nullptr, B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq);
    }
    // Else: `this` is unchanged, since these pre-move states match post-move
    // states: empty eptr, empty Try, small value uintptr.
  }

  // IMPORTANT: Must not read `this`, it may be after-dtor, or before-ctor
  constexpr void move_from_impl(rich_exception_ptr_impl&& other) noexcept {
    auto other_bits = other.get_bits();
    if (B::OWNS_EXCEPTION_PTR_and & other_bits) {
      assign_owned_eptr_and_bits(
          std::move(other.get_eptr_ref_guard().ref()), other_bits);
    } else if (B::NOTHROW_OPERATION_CANCELLED_eq == other_bits) {
      // Copy bit state storage, bypassing `rich_exception_ptr_impl::operator=`.
      // DO NOT run the `std::exception_ptr` move ctor; immortal OC is unowned.
      B::operator=(other);
    } else {
      // Immortal RE, empty eptr, empty Try, small value uintptr
      copy_unowned_pointer_sized_state(other);
    }
    other.make_moved_out(other_bits);
  }

 protected:
  // Implementation for `rich_exception_ptr::from_exception_ptr_slow`.
  //
  // Future: This sets to "unknown type" to avoid doing RTTI eagerly.  In
  // theory, our mutable getters could update `bits_` opportunistically.
  rich_exception_ptr_impl(force_slow_rtti_t, std::exception_ptr&& e) {
    assign_owned_eptr_and_bits(
        std::move(e),
        bits_t{
            B::OWNS_EXCEPTION_PTR_and | B::owned_eptr_UNKNOWN_TYPE_masked_eq});
  }

  template <typename, typename, auto...>
  friend class folly::immortal_rich_error_t;

  // Used only by `immortal_rich_error`.  IMPORTANT: In any new use, address
  // the pointer slicing risk flagged in the `IS_RICH_ERROR_BASE_...` docblock.
  // `consteval` guarantees `p` is immortal.
  template <typename Ex>
  consteval rich_exception_ptr_impl(
      const Ex* ex, const detail::immortal_exception_storage* p) {
    set_immortal_storage_and_bits(p, B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq);
    // Future: This constraint is to allow packing `rich_exception_ptr::bits_`
    // into the unused bits of the pointer.  Once `constexpr` allows some
    // pointer bit twiddling, we can trivially support constexpr
    // `OperationCancelled`, as well as non-`rich_error_base` exception types.
    static_assert( // Has manual test
        std::derived_from<Ex, rich_error_base>,
        "In C++20, `rich_exception_ptr` can only support immortal errors via "
        "`immortal_rich_error`.");
    // Redundant with `rich_error.h` asserts, so no manual test for this.
    static_assert(detail::has_offset0_base<Ex, rich_error_base>);
    if (ex->underlying_error()) {
      // This simplifying assumption is good for perf -- this means that
      // `with_underlying()` et al can test for "immortal rich error" and skip
      // querying `underlying_error()` which would otherwise require an extra
      // non-local load.  If this became needed, the load could be made local
      // by denormalizing `underlying_error()` into `get_immortal_storage()`.
      // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
      throw "For now, immortal errors don't support `underlying_error()`";
    }
  }

 public:
  constexpr ~rich_exception_ptr_impl() {
    destroy_owned_state_and_bits_caller_must_reset();
  }

  /// The default-constructed state is like an empty `std::exception_ptr`
  constexpr rich_exception_ptr_impl() {
    set_immortal_storage_and_bits(
        nullptr, B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq);
  }

  explicit rich_exception_ptr_impl(make_empty_try_t) { set_empty_try(); }

  /// This constructor makes an "owning" `rich_exception_ptr`.
  ///
  /// Future: When needed, in-place construct from a callable, via either of:
  ///   rich_exception_ptr rep{std::in_place_type<Ex>, ...}
  ///   auto rep = rich_exception_ptr::make_with([](){ return Ex{...}; });
  ///
  /// On supported platforms, this is FAR more efficient than throw-and-catch.
  /// Still, construction allocates, and copy/destroy perform atomic ops.
  ///
  /// For a cheap-to-make and cheap-to-copy "immortal" exception ptr, see
  /// `immortal_rich_error.h`.
  ///
  /// IMPORTANT NOTE: We do NOT want users to construct `rich_exception_ptr`
  /// from type-erased `std::exception_ptr` or `folly::exception_wrapper`,
  /// because that would break RTTI-avoidance optimizations in `result` code.
  template <std::derived_from<std::exception> Ex>
  explicit rich_exception_ptr_impl(Ex ex) {
    using bits_t = typename B::bits_t; // MSVC thinks `B::BIT_NAME` is private
    constexpr bits_t bits{[]() {
      if constexpr (std::derived_from<Ex, rich_error_base>) {
        // Redundant with `rich_error.h` asserts, hence no manual test.
        static_assert(detail::has_offset0_base<Ex, rich_error_base>);
        return bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::IS_RICH_ERROR_BASE_masked_eq;
      } else if constexpr (std::derived_from<Ex, OperationCancelled>) {
        // We only want the throwing version here, since nothrow OC uses a
        // different ctor, and a non-owned copy of a leaky singleton.  This
        // should never fire since `StubNothrowOperationCancelled` doesn't
        // derive from `std::exception`, and `OperationCancelled` will soon
        // no longer be directly constructible.
        static_assert(
            // FIXME: This one will go away:
            std::is_same_v<const Ex, const OperationCancelled> ||
            std::is_same_v<const Ex, const StubThrownOperationCancelled>);
        return bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::IS_OPERATION_CANCELLED_masked_eq;
      } else {
        return bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::owned_eptr_KNOWN_NON_FAST_PATH_TYPE_masked_eq;
      }
    }()};
    assign_owned_eptr_and_bits(
        make_exception_ptr_with(std::in_place, std::move(ex)), bits);
  }

  /// Copy constructor & assignment
  constexpr rich_exception_ptr_impl(const rich_exception_ptr_impl& other) {
    copy_from_impl(other);
  }
  rich_exception_ptr_impl& operator=(const rich_exception_ptr_impl& other) {
    if (this != &other) {
      destroy_owned_state_and_bits_caller_must_reset();
      copy_from_impl(other);
    }
    return *this;
  }

  /// Move constructor & assignment
  constexpr rich_exception_ptr_impl(rich_exception_ptr_impl&& other) noexcept {
    move_from_impl(std::move(other));
  }
  rich_exception_ptr_impl& operator=(rich_exception_ptr_impl&& other) noexcept {
    if (this != &other) {
      destroy_owned_state_and_bits_caller_must_reset();
      move_from_impl(std::move(other));
    }
    return *this;
  }

  // AVOID.  Prefer `rich_exception_ptr{YourErr{}}`, since knowing the type
  // statically can avoid RTTI for `rich_error.h` types, and can speed up
  // `result<T>::has_stopped()`.
  //
  // Pass by-&& because `std::exception_ptr` isn't always efficiently movable,
  // and we have a workaround for this.
  static Derived from_exception_ptr_slow(std::exception_ptr&& e) {
    if (!e) {
      return Derived{}; // "Owned" never stores an empty eptr.
    }
    return Derived{detail::force_slow_rtti_t{}, std::move(e)};
  }

  // PRIVATE, not for end users -- the non-stub type will be in `detail`.
  // Users will instead use `co_yield co_cancellet_nothrow` in coros.
  explicit rich_exception_ptr_impl(StubNothrowOperationCancelled) {
    // Wrapper that constructs the exception_ptr on first use.
    // No destructor needed - mutable_eptr_ref_guard is POD-like storage,
    // so the exception_ptr is leaked to avoid SDOF.
    struct InitializedSingleton : B::mutable_eptr_ref_guard {
      InitializedSingleton() {
        new (&this->ref()) std::exception_ptr{make_exception_ptr_with(
            std::in_place_type<StubNothrowOperationCancelled>)};
      }
    };
    // Meyer singleton is thread-safe past C++11.
    // Yes, this will make an instance per DSO. That's expected and OK.
    // NOLINTNEXTLINE(facebook-hte-InlinedStaticLocalVariableWarning)
    static InitializedSingleton singleton;
    B::apply_bits_after_setting_data_with(
        // Bitwise-copy the singleton without bumping the eptr's refcount
        [&](auto& d) { d.eptr_ref_guard_ = singleton; },
        B::NOTHROW_OPERATION_CANCELLED_eq);
  }
};

using rich_exception_ptr_base = rich_exception_ptr_impl<
    rich_exception_ptr,
    conditional_t<
        rich_exception_ptr_packed_storage::is_supported,
        rich_exception_ptr_packed_storage,
        rich_exception_ptr_separate_storage>>;

} // namespace detail

/// `rich_exception_ptr` is an analog of `exception_wrapper` or
/// `std::exception_ptr`, with some extra efficiency optimizations, and
/// integration with `rich_error` / `enrich_non_value`.  It was designed to
/// support rich-error features in `result.h`.
///
/// This class typically owns a `std::exception_ptr`, or stores a cheap-to-copy
/// pointer to an immortal exception.  Unlike `exception_wrapper`, it enables
/// RTTI-free lookup for `rich_error_base` & `result<T>::has_stopped()`.
///
/// However, private APIs can store some other states in its internal union.
/// For this reason, `rich_exception_ptr` should not be used in most end-user
/// code.  Instead, use `non_value_result`.
///
/// While generally aligned to `exception_wrapper`, this API is much smaller.
/// Notably, the ONLY way to test `rich_exception_ptr` for the presence of the
/// exception type `Ex` is via `folly::get_exception<Ex>(rich_eptr)`.
class rich_exception_ptr final : public detail::rich_exception_ptr_base {
  using detail::rich_exception_ptr_base::rich_exception_ptr_base;
};

} // namespace folly

#endif // FOLLY_HAS_RESULT
