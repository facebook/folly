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

// These are in `detail` since they're opt-build fallbacks for a debug-fatal.
struct bad_result_access_error : public std::exception {};
struct empty_result_error : public std::exception {};

// Stub types that will be replaced by integrations with other folly/ types.
//
// Will be replaced by real types from OperationCancelled.h, which will both
// derive from `OperationCancelled`.
struct StubNothrowOperationCancelled {}; // NOT an `std::exception`
struct StubThrownOperationCancelled : std::exception {};
// Will be replaced by the real types that are currently in Try.h
struct StubUsingUninitializedTry : std::exception {};
struct StubTryException : std::exception {};

// Free function to get a singleton exception_ptr for bad_result_access
const std::exception_ptr& bad_result_access_singleton();

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

  template <typename Ex>
  consteval static void assert_operation_cancelled_queries() {
    static_assert(
        !std::is_same_v<const Ex, const StubNothrowOperationCancelled> &&
            !std::is_same_v<const Ex, const StubThrownOperationCancelled>,
        "User code may only test for `OperationCancelled`; its derived classes "
        "are private implementation details.");
  }

  template <typename MaybeConstEx>
  MaybeConstEx* get_exception_from_nothrow_oc() const {
    B::debug_assert(
        "get_exception_from_nothrow_oc",
        B::NOTHROW_OPERATION_CANCELLED_eq == B::get_bits());
    const auto& epg = B::get_eptr_ref_guard();
    // Since `assert_operation_cancelled_queries` forbids testing for
    // the private derived classes of OC, we don't need `derived_from`.
    if constexpr (std::
                      is_same_v<const MaybeConstEx, const OperationCancelled>) {
      return static_cast<MaybeConstEx*>(exception_ptr_get_object(epg.ref()));
    }
    return nullptr; // Stored OC, but queried for other type.
  }

  template <typename MaybeConstEx>
  MaybeConstEx* get_exception_from_owned_eptr() const {
    B::debug_assert(
        "get_exception_from_owned_eptr",
        B::OWNS_EXCEPTION_PTR_and & B::get_bits());
    const auto& epg = B::get_eptr_ref_guard();
    const auto& ep = epg.ref();
    const auto fast_path_bits = B::mask_FAST_PATH_TYPES & B::get_bits();
    // Fast path for `result::has_stopped()`.  This is exhaustive since
    // `OperationCancelled` is the base for the only "official" cancellation
    // signals -- user derived classes are forbidden via
    // `assert_operation_cancelled_queries()`.
    if constexpr (std::
                      is_same_v<const MaybeConstEx, const OperationCancelled>) {
      if (fast_path_bits == B::IS_OPERATION_CANCELLED_masked_eq) {
        return static_cast<MaybeConstEx*>(exception_ptr_get_object(ep));
      } else if (fast_path_bits != B::owned_eptr_UNKNOWN_TYPE_masked_eq) {
        return nullptr; // Known type, but not `MaybeConstEx`
      }
      // ... fall through
    } else if constexpr (
        std::is_same_v<const MaybeConstEx, const rich_error_base>) {
      // Fast path for `rich_error_base`.  We currently don't try to accelerate
      // lookup of its derived classes, except for the "miss" case below.
      //
      // Future: See if knowing that the pointed-to type derives from
      // `rich_error_base` gives a faster `dynamic_cast` than our fallback.
      if (fast_path_bits == B::IS_RICH_ERROR_BASE_masked_eq) {
        // The owned eptr ctor checks `rich_error_base` has offset 0
        return static_cast<MaybeConstEx*>(exception_ptr_get_object(ep));
      } else if (fast_path_bits != B::owned_eptr_UNKNOWN_TYPE_masked_eq) {
        return nullptr; // Known type, but not `MaybeConstEx`
      }
      // ... fall through
    }
    if constexpr (std::derived_from<MaybeConstEx, rich_error_base>) {
      if (fast_path_bits != B::IS_RICH_ERROR_BASE_masked_eq &&
          fast_path_bits != B::owned_eptr_UNKNOWN_TYPE_masked_eq) {
        return nullptr;
      }
    }
    return exception_ptr_get_object_hint<MaybeConstEx>(ep);
  }

  static constexpr auto with_underlying_impl(auto* me, auto fn)
      -> decltype(fn(me));

  // Calls `fn` with the underlying exception, bypassing any chain of enriching
  // wrappers that `this` might represent, and returns the result.
  //
  // Note that `fn` must be generic, ready to accept either of:
  //   - The `rich_exception_ptr_impl<...>` sliced from `rich_exception_ptr`
  //     (default params!) returned by `rich_error_base::underlying_error()`, or
  //   - ... this `rich_exception_ptr_impl*`, if that's a different type.
  constexpr decltype(auto) with_underlying(auto fn) const {
    return with_underlying_impl(this, fn);
  }
  constexpr decltype(auto) with_underlying(auto fn) {
    return with_underlying_impl(this, fn);
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

  // Must call via `with_underlying()` -- code that rethrows may target `catch`,
  // so the only right behavior is to drop enriching wrappers.
  template <bool PartOfTryImpl> //  `true` only when called from `Try.h`.
  [[noreturn]] void throw_exception_impl() const {
    bits_t bits = B::get_bits();
    if (bits_t::OWNS_EXCEPTION_PTR_and & bits) {
      const auto& epg = B::get_eptr_ref_guard();
      const auto& ep = epg.ref();
      if (ep) {
        std::rethrow_exception(ep);
      }
      // Empty eptr, fall through to terminate...  Should not be reached --
      // `from_exception_ptr_slow` rewrites empty eptrs as immortal `nullptr`s.
      B::debug_assert("Bug: empty owned eptr stored inside a REP", false);
    } else if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
      if (auto* immortal_storage = get_immortal_storage()) {
        immortal_storage->throw_exception();
        compiler_may_unsafely_assume_unreachable(); // required for [[noreturn]]
      }
      // Empty eptr, fall through to terminate...
    } else if (bits_t::NOTHROW_OPERATION_CANCELLED_eq == bits) {
      // Don't bother with the stored eptr since re-throwing does not
      // preserve object identity.
      // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
      throw StubNothrowOperationCancelled{};
    } else if (B::SIGIL_eq == bits) {
      B::debug_assert(
          "throw_exception SIGIL_eq", B::get_uintptr() == B::kSigilEmptyTry);
      if (PartOfTryImpl) {
        // Match behavior of `Try::throwUnlessValue` ...  in some other usage
        // `Try` instead throws `TryException`, but luckily
        // `UsingUninitializedTry` derives from that.
        throw StubUsingUninitializedTry{};
      } else { // Match `result::value_or_throw()` behavior
        B::debug_assert("Cannot `throw_exception` on empty `Try`", false);
        throw empty_result_error{};
      }
    } else if (B::SMALL_VALUE_eq == bits) {
      if constexpr (PartOfTryImpl) {
        throw StubTryException{}; // Match `Try::exception()` behavior
      } else { // Match `result::non_value()` behavior
        B::debug_assert("Cannot `throw_exception` in value state", false);
        throw bad_result_access_error{};
      }
    }
    // Else: the above bit tests are intended to exhaustively cover all allowed
    // states, so we only fall through to terminate when:
    //   - Our bit state is invalid.
    //   - `this` contains an empty eptr.  Per the standard, rethrowing one is
    //     UB, and our current termination behavior matches `exception_wrapper`.
    // Future: maybe relax these to debug-fatal/opt-throw, like `result` does.
    B::terminate_on_empty_or_invalid_eptr(bits);
  }

  // Call ONLY after handling owned-eptr in `to_exception_ptr_...()`
  template <bool PartOfTryImpl> //  `true` only when called from `Try.h`.
  std::exception_ptr to_exception_ptr_non_owned(bits_t bits) const {
    if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
      const auto* s = get_immortal_storage();
      return s ? s->to_exception_ptr() : std::exception_ptr{};
    }
    if constexpr (PartOfTryImpl) {
      if (B::SIGIL_eq == bits) {
        // For empty `Try`, match behavior of `Try::throwUnlessValue` ... in
        // some other usage `Try` insetad throws `TryException`, but luckily
        // `UsingUninitializedTry` derives from that.
        throw StubUsingUninitializedTry{};
      } else if (B::SMALL_VALUE_eq == bits) {
        throw StubTryException{}; // Match `Try::exception()` behavior
      } // Else: fall through...
    }
    // We're NOT implementing `Try`, and are either in the small value state,
    // or in the empty `Try` state. Both are debug-fatal. For simplicity,
    // we have both match `result::non_value()` failure behavior.
    B::debug_assert(
        "Cannot use `to_exception_ptr_slow` in value or empty `Try` state",
        false);
    return bad_result_access_singleton();
  }

  template <bool PartOfTryImpl> //  `true` only when called from `Try.h`.
  std::exception_ptr to_exception_ptr_copy() const {
    // Code that examines `exception_ptr` may rethrow or use legacy APIs, so we
    // have to drop enriching wrappers.
    return with_underlying([](auto* rep) {
      auto bits = rep->get_bits();
      if ((B::OWNS_EXCEPTION_PTR_and & bits) ||
          // Future: It's quite silly that this copy op, and its eventual dtor,
          // both contend on the atomic refcount for the singleton...  when a
          // leaky singleton doesn't even need refcounts.  See the
          // `get_outer_exception` doc for a bithacking idea to fix this.
          (B::NOTHROW_OPERATION_CANCELLED_eq == bits)) {
        return rep->get_eptr_ref_guard().ref();
      }
      return rep->template to_exception_ptr_non_owned<PartOfTryImpl>(bits);
    });
  }

  template <bool PartOfTryImpl> //  `true` only when called from `Try.h`.
  std::exception_ptr to_exception_ptr_move() {
    // Similar logic to `to_exception_ptr_copy()`, but we are moving out the
    // inner exception -- so that needs to be "moved out".  Then, any outer
    // exceptions need to be destroyed.
    std::exception_ptr eptr;
    bits_t outer_bits = B::get_bits();
    using bits_t = typename B::bits_t; // MSVC thinks `B::BIT_NAME` is private
    bool this_is_wrapper = with_underlying([&eptr](auto* rep) {
      bits_t bits = rep->get_bits();
      // `with_underlying` makes `rep` a pointer-to-`const` iff `this` is not
      // an underlying error, or equivalently if `rep` is not an outer error.
      constexpr bool rep_is_not_outer =
          std::is_const_v<std::remove_pointer_t<decltype(rep)>>;
      if (bits_t::OWNS_EXCEPTION_PTR_and & bits) {
        if constexpr (rep_is_not_outer) {
          // When `rep` is not `this`, it comes from `with_underlying`, which
          // (by design!) points to `const`.  So, the `else` branch shouldn't
          // compile (again, by design), but more importantly, it would violate
          // the principle that enrichment wrappers are transparent.  The doc
          // of `rich_error_base::mutable_underlying_error` speaks to this.
          // Concretely:
          //   rich_exception_ptr rep1 = /* enrichment-wrapped MyErr */;
          //   auto rep2 = rep1; // Both share the same `enriched_non_value`!
          // This would mutate `enriched_non_value::next_` for *both*.
          //   auto eptr2 = std::move(rep2).to_exception_ptr_slow();
          // So `rep1` would now be a wrapper around a moved-out (empty) REP.
          eptr = rep->get_eptr_ref_guard().ref();
        } else { // If `rep` is `this`, then we can safely move it.
          eptr = detail::extract_exception_ptr(
              std::move(rep->get_eptr_ref_guard().ref()));
        }
      } else if (bits_t::NOTHROW_OPERATION_CANCELLED_eq == bits) {
        // For nothrow OC, the stored eptr is a non-owning copy of a leaky
        // singleton. We must copy (not move/extract) to properly increment
        // refcounts, matching the behavior of `to_exception_ptr_copy()`.
        //
        // Future: See `to_exception_ptr_copy` for an optimization idea.
        eptr = rep->get_eptr_ref_guard().ref();
      } else {
        eptr = rep->template to_exception_ptr_non_owned<PartOfTryImpl>(bits);
      }
      return rep_is_not_outer;
    });
    if (this_is_wrapper) {
      // We just moved out the underlying error, but the wrapper chain still
      // needs to be destroyed, or `make_moved_out` would leak it.
      destroy_owned_state_and_bits_caller_must_reset();
    }
    make_moved_out(outer_bits);
    return detail::extract_exception_ptr(std::move(eptr));
  }

  template <typename D1, typename B1, typename D2, typename B2>
  static constexpr bool immortal_compares_equal(
      // `B1` & `B2` may differ, see `operator==` and `compare_equal`
      const rich_exception_ptr_impl<D1, B1>* immortal,
      const rich_exception_ptr_impl<D2, B2>* other,
      bits_t other_bits) {
    if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == other_bits) {
      // Why aren't we comparing `get_immortal_storage()->immortal_ptr_`?
      //  (1) We only expect immortal `rich_exception_ptr`s to be created via
      //      `immortal_rich_error`, so it is fine to assume that each immortal
      //      exception has a unique `get_immortal_storage()`.
      //  (2) `get_immortal_storage()` is null for `rich_exception_ptr` empty.
      return immortal->get_immortal_storage() == other->get_immortal_storage();
    } else if (B::OWNS_EXCEPTION_PTR_and & other_bits) {
      if (auto* immortal_storage = immortal->get_immortal_storage()) {
        // An immortal eptr can only compare equal to an owned one if the latter
        // is a copy of the immortal's mutable singleton.  We don't want to
        // eagerly create that singleton here, so instead check whether it has
        // already been created.
        //
        // The right memory ordering here is TBD, but its impact is low, since
        // it would only save a few cycles of "acquire" on ARM (acquire loads
        // are free on x86).  For now, we go with (2), the more conservative of
        // two conflicting points of view:
        //
        // (1) We may test for "is created" with a relaxed load of the
        //     underlying `eptr_`, and only load the ref to the value when it
        //     is known to be there.
        //
        // (2) Must use "acquire" ordering for happens-before synchronization
        //     with the store-release in the singleton's `ensure_created()`.
        //     On weaker memory models (e.g.  ARM), using "relaxed" here could
        //     read a stale `nullptr` after `other` was copied from the same
        //     singleton, causing us to incorrectly return `false`.
        if (auto* mutable_eptr =
                immortal_storage
                    ->acquire_mutable_singleton_ptr_if_already_created()) {
          return *mutable_eptr == other->get_eptr_ref_guard().ref();
        }
        return false;
      } else { // `immortal` is empty
        return !other->get_eptr_ref_guard().ref();
      }
    }
    B::debug_assert(
        "operator== / immortal exhaustiveness check",
        // These states aren't errors
        B::SMALL_VALUE_eq == other_bits || B::SIGIL_eq == other_bits ||
            // Has eptr, but will never equal an immortal rich eptr, since
            // nothrow OC uses its own separate singleton.
            B::NOTHROW_OPERATION_CANCELLED_eq == other_bits);
    return false;
  }

  // Must call via `with_underlying()` -- we want to compare the innermost
  // exception, ignoring enriching wrappers.
  template <typename D1, typename B1, typename D2, typename B2>
  static constexpr bool compare_equal(
      const rich_exception_ptr_impl<D1, B1>* lp,
      // The doc of `operator==` explains the argument asymmetry.
      const rich_exception_ptr_impl<D2, B2>* rp) {
    const auto lbits = lp->get_bits(), rbits = rp->get_bits();
    if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == lbits) {
      return immortal_compares_equal(lp, rp, rbits);
    } else if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == rbits) {
      // Future: Check if it's worth manually pruning the "immmortal
      // other_bits" branch within this call (it was covered above).
      return immortal_compares_equal(rp, lp, lbits);
    } else if (
        ((B::OWNS_EXCEPTION_PTR_and & lbits) ||
         B::NOTHROW_OPERATION_CANCELLED_eq == lbits) &&
        ((B::OWNS_EXCEPTION_PTR_and & rbits) ||
         B::NOTHROW_OPERATION_CANCELLED_eq == rbits)) {
      return lp->get_eptr_ref_guard().ref() == rp->get_eptr_ref_guard().ref();
    } else if (B::SMALL_VALUE_eq == lbits && B::SMALL_VALUE_eq == rbits) {
      B::debug_assert(
          "rich_exception_ptr::operator== invalid for 2 small value uintptrs",
          false);
      // The fallback could trivally compare `get_uintptr()`, to return `true`
      // correctly sometimes, but the the right comparison semantics must
      // actually be determined by `result<T>` that knows the small-value type.
      return false;
    }
    // Cases now ruled out:
    //   - Either side is an immortal rich error.
    //   - Both sides use eptr_ref_guard (either owned or nothrow OC).
    //   - Both sides hold small value uintptrs.
    //
    // Those covered all comparisons that can go "across" bit states, namely
    // comparing the various kinds of errors.  All the remaining heterogeneous
    // comparisons are false:
    //   small value uintptr VS eptr_ref_guard
    //   sigil (empty Try) VS eptr_ref_guard
    //   sigil (empty Try) VS small value uintptr
    //
    // This means that the only possible true comparison is "sigil VS sigil".
    //
    // NB: Here, we assume that we're part of the `Try` impl if the bits are
    // `SIGIL_eq` -- this is done to avoid templating the container on "is it
    // in `Try`?" to make it easier to interconvert `Try` and `result`.
    if (B::SIGIL_eq == lbits && B::SIGIL_eq == rbits) {
      B::debug_assert(
          "operator== SIGIL_eq",
          lp->get_uintptr() == B::kSigilEmptyTry &&
              rp->get_uintptr() == B::kSigilEmptyTry);
    } else {
      B::debug_assert("operator== !SIGIL_eq", lbits != rbits);
    }
    return lbits == rbits;
  }

 protected:
  // Implementation for `rich_exception_ptr::from_exception_ptr_slow`.
  //
  // Future: This sets to "unknown type" to avoid doing RTTI eagerly.  In
  // theory, our mutable getters could update `bits_` opportunistically.
  rich_exception_ptr_impl(force_slow_rtti_t, std::exception_ptr&& e) {
    assign_owned_eptr_and_bits(
        std::move(e),
        static_cast<bits_t>(
            B::OWNS_EXCEPTION_PTR_and | B::owned_eptr_UNKNOWN_TYPE_masked_eq));
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
    constexpr bits_t bits{[]() -> bits_t {
      if constexpr (std::derived_from<Ex, rich_error_base>) {
        // Redundant with `rich_error.h` asserts, hence no manual test.
        static_assert(detail::has_offset0_base<Ex, rich_error_base>);
        return static_cast<bits_t>(
            bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::IS_RICH_ERROR_BASE_masked_eq);
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
        return static_cast<bits_t>(
            bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::IS_OPERATION_CANCELLED_masked_eq);
      } else {
        return static_cast<bits_t>(
            bits_t::OWNS_EXCEPTION_PTR_and |
            bits_t::owned_eptr_KNOWN_NON_FAST_PATH_TYPE_masked_eq);
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

  /// Throws the innermost exception, ignoring enriching wrappers.
  ///
  /// Precondition: Contains a nonempty exception.  Terminates on empty
  /// `exception_ptr`, on invalid internal state.  Small-value is debug-fatal.
  [[noreturn]] void throw_exception() const {
    with_underlying([](auto* rep) {
      rep->template throw_exception_impl</*PartOfTryImpl=*/false>();
    });
    compiler_may_unsafely_assume_unreachable(); // required for [[noreturn]]
  }
  // PRIVATE TO `Try`: `throw_exception()` with slight behavior differences.
  [[noreturn]] void throw_exception(try_rich_exception_ptr_private_t) const {
    with_underlying([](auto* rep) {
      rep->template throw_exception_impl</*PartOfTryImpl=*/true>();
    });
    compiler_may_unsafely_assume_unreachable(); // required for [[noreturn]]
  }

  /// Returns the `std::exception_ptr` for the innermost exception, DISCARDING
  /// enriching wrappers.
  ///
  /// Overload differences:
  ///   - `const&` copies the inner eptr (cost: an atomic refcount increment).
  ///   - `&&` moves the inner eptr, destroys any enriching wrappers, and
  ///     leaves `this` in a moved-out, empty eptr state.
  ///
  /// Precondition: Contains an exception, or empty eptr (debug-fatal otherwise)
  [[nodiscard]] std::exception_ptr to_exception_ptr_slow() const& {
    return to_exception_ptr_copy</*PartOfTryImpl=*/false>();
  }
  [[nodiscard]] std::exception_ptr to_exception_ptr_slow() && {
    return to_exception_ptr_move</*PartOfTryImpl=*/false>();
  }
  // PRIVATE TO `Try`: `to_exception_ptr_slow()` with edge case differences.
  [[nodiscard]] std::exception_ptr to_exception_ptr_slow(
      try_rich_exception_ptr_private_t) const& {
    return to_exception_ptr_copy</*PartOfTryImpl=*/true>();
  }
  [[nodiscard]] std::exception_ptr to_exception_ptr_slow(
      try_rich_exception_ptr_private_t) && {
    return to_exception_ptr_move</*PartOfTryImpl=*/true>();
  }

  /// Returns the `typeid` of the innermost exception object, ignoring
  /// enriching wrappers.  If no exception object is stored, returns null.
  ///
  /// Future: Could perhaps be made public, currently PRIVATE due to the API
  /// design issue with immortals documented inline.  That would simplify the
  /// `rich_error_base` plumbing.
  constexpr const std::type_info* exception_type(
      rich_error_base::private_get_exception_ptr_type_t) const noexcept {
    using bits_t = typename B::bits_t; // MSVC thinks `B::BIT_NAME` is private
    return with_underlying([](auto* rep) -> const std::type_info* {
      if (bits_t::OWNS_EXCEPTION_PTR_and & rep->get_bits()) {
        return exception_ptr_get_type(rep->get_eptr_ref_guard().ref());
      } else if (
          bits_t::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == rep->get_bits()) {
        // This shouldn't even be hit on our internal formatting code path.
        //
        // FIXME: This is potentially too confusing for a public API, since:
        //  - The actual storage type is `immortal_rich_error_storage`, which
        //    is not user-addressable.
        //  - The user can get `const UserBase*`, `rich_error<UserBase>*>, or
        //    `const rich_error<UserBase>*`, but the latter 2 have costs,
        //    and all 3 are distinct objects.
        return rep->get_immortal_storage()
            ? rep->get_immortal_storage()->user_base_type_
            : nullptr;
      } else if (bits_t::NOTHROW_OPERATION_CANCELLED_eq == rep->get_bits()) {
        return &typeid(StubNothrowOperationCancelled);
      }
      return nullptr; // Non-exceptions: empty `Try`, small value uintptr
    });
  }

  /// Returns `true` when both `lhs` and `rhs`...
  ///  - ... point at the same underlying exception, per the details below.
  ///  - ... occur in the `Try` implementation, and both contain empty `Try`.
  ///
  /// When `lhs` and `rhs` are both representable as eptrs, we compare the
  /// underlying exception object **pointers** -- ignoring enriching wrappers.
  ///
  /// Caveat 1: If the same `immortal_rich_error<...>::ptr()` is instantiated
  /// in multiple DSOs, then you may end up with multiple copies of the
  /// underlying error, and their pointers will compare unequal.  To stop this,
  /// pick a single link unit to instantiate your `immortal_rich_error`, and
  /// expose only the `rich_exception_ptr` via the corresponding header.
  ///
  /// Caveat 2: Since `std::exception_ptr` isn't `constexpr`, an immortal rich
  /// errors lazily makes an eptr singleton once needed.  Actually, there are
  /// two singletons -- immutable & mutable -- for reasons discussed on
  /// `get_outer_exception()`.  It is worth knowing that `operator==` can
  /// compare an "owned eptr" with the mutable singleton of a "immortal rich
  /// error".  This will happen, for example, if you copy
  /// `to_exception_ptr_slow`:
  ///
  ///   auto rep1 = immortal_error<YourErr>::get();
  ///   auto rep2 = rich_exception_ptr::from_exception_ptr_slow(
  ///       rep.to_exception_ptr_slow());
  ///   assert(rep1 == rep2);
  ///
  /// The immutable singleton is never exposed as an eptr, so the 2-singleton
  /// inconsistency is not observable via this `operator==`.
  template <typename D2, typename B2>
  friend constexpr inline bool operator==(
      const rich_exception_ptr_impl& lhs,
      // The template setup is asymmetric, but the implementation should anyhow
      // be symmetric in lhs & rhs, so I don't expect issues from that.
      //
      // Besides symmetry, the key implementation constraint is NEVER to
      // use the union members or `bits_`.  Always, use the base-independent
      // accessors -- this is required to support cross-base comparison.
      //
      // Note that this doesn't allow cross-value comparisons YET, just like
      // `folly::result`, but this can be extended to follow `std::expected`.
      const rich_exception_ptr_impl<D2, B2>& rhs) {
    return lhs.with_underlying([&rhs](const auto* lp) {
      return rhs.with_underlying([lp](const auto* rp) {
        // MSVC needs full qualification here
        return rich_exception_ptr_impl<Derived, B>::compare_equal(lp, rp);
      });
    });
  }

  // Like `folly::get_exception<Ex>()`, but accesses the actual outermost
  // exception, even if it's wrapping another one -- instead of walking to the
  // underlying error.
  //
  // CAREFUL: The type of this exception is different from whatever error
  // actually occurred, see `enrich_non_value.h` for the most common example.
  //
  // This is used by `rich_error_base::format_to` to walk the enrichment chain.
  template <typename Ex>
  constexpr Ex const* get_outer_exception() const noexcept
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    assert_operation_cancelled_queries<Ex>();
    bits_t bits = B::get_bits();
    if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
      if (!get_immortal_storage()) {
        return nullptr; // empty eptr
      }

      // The goal of the branches below is to hide from the end user that
      // (until C++26) `rich_error<UserBase>` cannot be `constexpr`, and thus
      // `immortal_ptr_` is `immortal_rich_error_storage<UserBase>`.
      //
      // So, the below tests for `folly_detail_base_of_rich_error` (2nd) and
      // `std::exception` (1st) aren't just optimizations, we need them to
      // correctly query `Ex` types that derive from `std::exception`.
      //
      // Internally, both `get_immortal_storage()->as_immutable_*` calls
      // instantiate an immortal singleton of `rich_error<UserBase>` with a
      // copy of the `constexpr` instance of `UserBase`.
      if constexpr (std::is_same_v<const std::exception, const Ex>) {
        return get_immortal_storage()->as_immutable_std_exception();
      }

      const rich_error_base* immortal_ptr =
          get_immortal_storage()->immortal_ptr_;

      // WATCH OUT: The `rich_error<Err>` part of this "fast path" logic is NOT
      // best-effort, but mandatory, see the prior comment.
      //
      // The "exact type" tests below can never pass if `Ex` isn't related to
      // `rich_error_base` (e.g.  `std::runtime_error`), and the `static_cast`
      // won't even compile.
      if constexpr (std::derived_from<Ex, rich_error_base>) {
        // Querying for `rich_error<Err>`?
        if constexpr (is_detected_v<
                          detect_folly_detail_base_of_rich_error,
                          Ex>) {
          // Querying for `rich_error<Err>` when the immortal has `Err`.
          if (&typeid(Ex) == get_immortal_storage()->rich_error_leaf_type_ ||
              // We MUST fall back to the slower comparison of `type_info`
              // objects, since in multi-DSO programs, there may exist multiple
              // `type_info` pointers for the same type.
              typeid(Ex) == *get_immortal_storage()->rich_error_leaf_type_) {
            return static_cast<const Ex*>(
                get_immortal_storage()->as_immutable_leaf_rich_error());
          }
          // Since we compared `type_info`s (and not just pointers), there's no
          // need to fall back to `dynamic_cast` -- `rich_error<>` is final.
          return nullptr;
        } else { // Querying for non-leaf (abstract) `Ex`...
          // Querying for `Ex` when the immortal stores `Ex`.
          //
          // Best-effort "fast path" to avoid RTTI when the query type exactly
          // matches the stored type.  This optimization may fail in multi-DSO
          // code (where many copies of the same `type_info` may exist at
          // different addresses).  Like `exception_ptr_get_object_hint`, this
          // gives a nice speedup for the common case.
          //
          // Only compare pointers, not `type_info` -- falling back to
          // `dynamic_cast` will do that part anyway.
          //
          // Future: Neither this branch, nor the `get_mutable_exception` one
          // uses `Ex::folly_get_exception_hint_types`, since the "exact type"
          // checks take care of the common case.  Implementing hint support is
          // possible, but first we need a use-case where it matters.
          if (&typeid(Ex) == get_immortal_storage()->user_base_type_) {
            return static_cast<const Ex*>(immortal_ptr);
          }
        }
      }
      // Fallback. Cheap & RTTI-free only when `Ex == rich_error_base`.
      //
      // Future: See `docs/future_fast_rtti.md` for how to avoid RTTI entirely.
      //
      // Future: If we do later support non-rich immortals, keep in mind that
      // `dynamic_cast` only works when the pointed-to expression is
      // polymorphic -- deriving from `std::exception` or `rich_error_base` is
      // fine.  We may require one of these bases for immortals.
      return dynamic_cast<const Ex*>(immortal_ptr);
    } else if (B::OWNS_EXCEPTION_PTR_and & bits) {
      return get_exception_from_owned_eptr<const Ex>();
    } else if (B::NOTHROW_OPERATION_CANCELLED_eq == bits) {
      return get_exception_from_nothrow_oc<const Ex>();
    }
    // The remaining bit states are not exceptions, so `nullptr` is a good
    // match for `folly::get_exception(result<T>)` semantics.
    B::debug_assert(
        "const get_outer_exception unexpected bits",
        B::SIGIL_eq == bits || B::SMALL_VALUE_eq == bits);
    return nullptr;
  }

  /// See docs on the `const` overload of `get_outer_exception<Ex>()`.  The
  /// mutable one mainly exists to implement `folly::get_mutable_exception<>()`.
  ///
  /// IMPORTANT: For immortal errors, there is no read-after-write consistency
  /// in the sense that you might expect.  Rather, this returns a pointer into
  /// a mutable, immortal singleton of `rich_error<UserBase>` that is
  /// completely separate from the one that the `const` overload accesses.
  ///
  /// ## Why don't you have ONE read/write singleton for immortals?
  ///
  /// We could, but it would broadly add cost for a use-case that shouldn't
  /// really come up.  If your edge-case usage relies on mutable access to an
  /// immortal, you're probably aware of this gotcha, and can arrange to
  /// call `get_mutable_exception` or the non-`const` overload of
  /// `get_outer_exception` where it matters.
  ///
  /// Here's a closer look at the downsides of a 1-singleton implementation:
  ///   - `get_exception<UserBase>()` & `get_exception<rich_error<UserBase>>()`
  ///     must access the same instance.  To achieve this, immortals would need
  ///     a mutable pointer to redirect from the constexpr `UserBase` to the
  ///     immortal singleton, as soon as the first mutable access happens.
  ///   - This pointer needs to be a relaxed read / relaxed write atomic,
  ///     which, while not very costly, adds indirection.  This also implies a
  ///     second indirection, for a virtual `immortal_ptr()` instead of the
  ///     current `immortal_ptr_`.  Finally, there'd be an extra branch.  The
  ///     expected extra cost is 3-5ns without memory contention, which is as
  ///     much as the current `get_rich_error()` latency.
  ///   - A further plus for the current 2-singletons implementation is that
  ///     it’s technically possible to avoid the atomic-increment cost of
  ///     `to_exception_ptr_slow()` through some bithacking.  Namely, the
  ///     mutable singleton `std::exception_ptr` is initially prepared with a
  ///     Very High Refcount of 2^63, and we reset it if it goes too low or too
  ///     high.  A concrete pattern would involve a relaxed-atomic check of the
  ///     first 2 bits.  If the bits are 00, or 11, then reset back to 2^63.
  ///     This allows safely handing out copies without bumping the refcount --
  ///     it would only break if a user simultaneously stored over 2^63 eptrs.
  template <typename Ex>
  constexpr Ex* get_outer_exception() noexcept
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    assert_operation_cancelled_queries<Ex>();
    // This mirrors the `const` overload, read that first for the comments.
    bits_t bits = B::get_bits();
    if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
      if (!get_immortal_storage()) {
        return nullptr;
      }
      B::debug_assert(
          "mutable get_outer_exception",
          B::get_bits() == B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq);
      if constexpr (std::is_same_v<const std::exception, const Ex>) {
        return get_immortal_storage()->as_mutable_std_exception();
      }
      auto* leaf_rich_error =
          get_immortal_storage()->as_mutable_leaf_rich_error();
      if constexpr (std::derived_from<Ex, rich_error_base>) {
        // Try to avoid RTTI when the immortal stores `Err` and the query is
        // either `Err` or `rich_error<Err>`.
        auto* type_to_match =
            is_detected_v<detect_folly_detail_base_of_rich_error, Ex>
            // NB: This could "miss" 8x faster by reusing the `const` pattern,
            // but...  people really shouldn't use mutable errors, anyhow!
            ? get_immortal_storage()->rich_error_leaf_type_
            : get_immortal_storage()->user_base_type_;
        // Best-effort optimization, may fail in multi-DSO code: Compare only
        // ptrs, not `type_info` -- `dynamic_cast` will do that part anyway.
        if (&typeid(Ex) == type_to_match) {
          return static_cast<Ex*>(leaf_rich_error);
        }
      }
      return dynamic_cast<Ex*>(leaf_rich_error);
    } else if (B::OWNS_EXCEPTION_PTR_and & bits) {
      return get_exception_from_owned_eptr<Ex>();
    } else if (B::NOTHROW_OPERATION_CANCELLED_eq == bits) {
      return get_exception_from_nothrow_oc<Ex>();
    }
    B::debug_assert(
        "mutable get_outer_exception unexpected bits",
        B::SIGIL_eq == bits || B::SMALL_VALUE_eq == bits);
    return nullptr;
  }

 private:
  // Calls `get_outer_exception` on the underlying error in the enrichment chain
  template <typename CEx>
  static constexpr rich_ptr_to_underlying_error<CEx> get_exception_impl(
      auto* rep);

 public:
  /// Implementation of `folly::get_exception<Ex>(rich_exception_ptr)`
  ///
  /// Transparently handles errors with enriching wrappers -- the returned
  /// pointer-like resolves to the underlying, original exception. But, `fmt` or
  /// `ostream::operator<<` will display the full enrichment chain.
  ///
  /// Avoid converting the result to `Ex*`, or you will lose the enrichments.
  ///
  /// Sample usage:
  ///
  ///   if (auto ex = get_exception<Ex>(...)) { // NOT `Ex* ex`!
  ///     LOG(INFO) << ex; // Will include enrichments
  ///   }
  ///
  /// IMPORTANT: For immortal errors, this `const` accessor will access a
  /// different instance than `get_mutable_exception` does.  This will not
  /// matter for 99% of usage, since mutating exceptions is VERY rare, and
  /// likely a design smell.  In short, this was done is to avoid globally
  /// imposing the cost of synchronization for the sake of a tiny minority
  /// use-case.  The non-`const` overload of `get_outer_exception` says more.
  ///
  /// Future: There's no return state for "did not match `Ex` (aka `nullptr`),
  /// but still have enrichments" -- but it is easy to add if useful.
  template <typename Ex>
  constexpr rich_ptr_to_underlying_error<const Ex> get_exception(
      get_exception_tag_t) const noexcept {
    return get_exception_impl<const Ex>(this);
  }
  // IMPORTANT: For immortal errors, this will access a different instance than
  // `get_exception` does, see the details in the non-`const` overload of
  // `get_outer_exception`.
  template <typename Ex>
  constexpr rich_ptr_to_underlying_error<Ex> get_mutable_exception(
      get_exception_tag_t) noexcept {
    return get_exception_impl<Ex>(this);
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
class [[nodiscard]]
rich_exception_ptr final : public detail::rich_exception_ptr_base {
  using detail::rich_exception_ptr_base::rich_exception_ptr_base;
};

namespace detail {

template <typename Derived, typename B>
/*static*/ constexpr inline auto
rich_exception_ptr_impl<Derived, B>::with_underlying_impl(auto* me, auto fn)
    -> decltype(fn(me)) {
  // Why only these bits, and no others?
  //   - The `consteval rich_exception_ptr_impl` ctor guarantees that immortals
  //     don't have a `underlying_error()` & explains how to extend if needed.
  //   - None of the other states, nothrow OC included, are rich errors.
  if (B::OWNS_EXCEPTION_PTR_and & me->get_bits()) {
    if (const auto* rex = me->template get_outer_exception<rich_error_base>()) {
      // The doc of `mutable_underlying_error()` explains why the `const` here
      // is crucial.  Also, `to_exception_ptr_move` logic above relies on this.
      if (const auto* rep = rex->underlying_error()) {
        return fn(rep);
      }
    }
  }
  return fn(me);
}

// Specializes the `with_underlying()` traversal for `get_exception<>()`, while
// populating `top_rich_error_` to support enriched formatting.
//
// Future: A possible micro-optimization idea to try to save 1-2ns would be to
// deduplicate the PLT call to `exception_ptr_get_object` (one per
// `get_outer_exception` call below).
template <typename Derived, typename B>
template <typename CEx>
constexpr inline auto rich_exception_ptr_impl<Derived, B>::get_exception_impl(
    auto* rep) -> rich_ptr_to_underlying_error<CEx> {
  assert_operation_cancelled_queries<CEx>();
  const rich_error_base* top_rich_error = nullptr;
  auto bits = rep->get_bits();
  if (B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq == bits) {
    // Per the `consteval rich_exception_ptr_impl` ctor, all immortals are
    // underlying.  So, they get special logic to set `top_rich_error_`.
    if constexpr (std::derived_from<CEx, rich_error_base>) {
      auto* ex = rep->template get_outer_exception<CEx>();
      return {ex, static_cast<const rich_error_base*>(ex)};
    } else if constexpr (!std::is_same_v<const CEx, const std::exception>) {
      // Immortals are rich, but query is neither std::exception, nor rich.
      return {nullptr, nullptr};
    } // else: `std::exception` IS accessible on immortals, needs 2 queries:
    top_rich_error = rep->template get_outer_exception<rich_error_base>();
    // Fall through to query for `CEx` in the final `return`...
  } else if (B::OWNS_EXCEPTION_PTR_and & bits) {
    // This dynamic eptr may be an enrichment wrapper, so we have to retrieve
    // the underlying error.  This gives us `top_rich_error_` for free.
    if (auto* rex = rep->template get_outer_exception<rich_error_base>()) {
      top_rich_error = rex;
      if (auto* new_rep = [rex]() {
            if constexpr (std::is_const_v<CEx>) {
              return rex->underlying_error();
            } else {
              return rex->mutable_underlying_error(
                  rich_error_base::underlying_error_private_t{});
            }
          }()) {
        // We found the underlying error, time to query `CEx`!
        // NB: Falling through with `rep = new_rep;` emitted slower code.
        return {new_rep->template get_outer_exception<CEx>(), top_rich_error};
      } else if constexpr (std::is_same_v<const CEx, const rich_error_base>) {
        // `rex` is underlying error, and exactly what was queried
        return {rex, top_rich_error};
      } // Else: `rep` is underlying & `top_rich_error` is set; fall through...
    } else if constexpr (std::derived_from<CEx, rich_error_base>) {
      // Outer isn't a `rich_error_base`, so it's also the underlying error,
      // and thus cannot match any `rich_error_base`-derived `CEx`.
      return {nullptr, nullptr};
    } // Else: `rep` is underlying & non-rich; `top_rich_error` stays null...
    // ... fall through to query `CEx`.
  } // Else: We only need to resolve underlying for "owned" bits.
  return {rep->template get_outer_exception<CEx>(), top_rich_error};
}

} // namespace detail

} // namespace folly

#endif // FOLLY_HAS_RESULT
