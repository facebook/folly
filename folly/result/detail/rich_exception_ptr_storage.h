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
#include <folly/result/detail/immortal_exception_storage.h>

#include <cstdint>
#include <new>

#if FOLLY_HAS_RESULT

// These are implementation details of `rich_exception_ptr`, used via private
// inheritance to enforce that users don't have to look at this file to find
// its public interface.

namespace folly::detail {

class rich_exception_ptr_base_storage {
 protected:
  // Non-owning view of type-punned storage of the `std::exception_ptr` stored
  // in a `uintptr_t` array.
  struct const_eptr_ref_guard {
    // On GNU systems `exception_ptr` contains 1 pointer; MSVC uses 2.
    // See https://devblogs.microsoft.com/oldnewthing/20200820-00/?p=104097
    static_assert(sizeof(std::exception_ptr) % sizeof(uintptr_t) == 0);
    static constexpr size_t detail_num_ptrs_in_exception_ptr =
        sizeof(std::exception_ptr) / sizeof(uintptr_t);
#if defined(_WIN32)
    static_assert(detail_num_ptrs_in_exception_ptr == 2);
#else
    static_assert(detail_num_ptrs_in_exception_ptr == 1);
#endif

    // NB: Since, in practice, all major compiler support type-punning through
    // unions, it'd be safer / cleaner to have an anon-union of either the
    // `uintptr_t`s (so packed storage can touch the lower bits) or a
    // `std::exception_ptr`.  Unfortunately, the latter would prevent the use
    // of this type in constant-evaluated code, so we have to stick with the
    // riskier `reinterpret_cast`.
    alignas(std::exception_ptr)
        uintptr_t uintptrs_[detail_num_ptrs_in_exception_ptr];

    const std::exception_ptr& ref() const {
      return *std::launder(
          reinterpret_cast<const std::exception_ptr*>(uintptrs_));
    }
  };
  // Separate type so `rich_exception_ptr_packed_storage::get_eptr_ref_guard`
  // can propagate `const`.
  struct mutable_eptr_ref_guard : const_eptr_ref_guard {
    std::exception_ptr& ref() {
      return *std::launder(reinterpret_cast<std::exception_ptr*>(uintptrs_));
    }
  };

  // On supported platforms, we overwrite the alignment bits (the low 3) of
  // `sigil_` (aka `immortal_storage_` aka `eptr_ref_guard_.uintptrs_[0]`) to
  // store `bits_`.  Note that on Windows, both indices 0 & 1 in
  // `eptr_ref_guard_.uintptrs_` are pointers.
  //
  // We check that the 3 bits are free via `static_assert` & `debug_assert`.
  //
  // NB Here, we are not constrained by the alignment requirements of any
  // "small value" type, since it is not directly accessed from this storage.
  // Rather, it is demangled by `small_value_ref_guard` from `get_uintptr()`.
  union
      // Below, we assert that eptr has the highest alignment of these members.
      alignas(std::exception_ptr) data_t {
    // (1) Used as a convenience to access packed bits for all union states.
    // (2) With `SIGIL_eq == bits`, would currently only store `kSigilEmptyTry`.
    // (3) With `SMALL_VALUE_eq == bits`, stores a bit-mangled small value.
    uintptr_t uintptr_;
    // NB: This {}-init is here to silence a linter.  It's not meant to do
    // anything, although it looks like it MIGHT slightly shift benchmarks.
    // Leaving it be, it's not yet worthwhile to really quantify its impact.
    const detail::immortal_exception_storage* immortal_storage_{};
    // Used for "owned eptr" and (non-owned) "nothrow OC leaky singleton eptr".
    mutable_eptr_ref_guard eptr_ref_guard_;
  };
  static_assert(
      alignof(std::exception_ptr) >=
      alignof(detail::immortal_exception_storage*));
  static_assert(alignof(std::exception_ptr) >= alignof(uintptr_t));

  constexpr static uintptr_t kSigilEmptyTry = 0; // See also `SIGIL_eq`
  static_assert(!(kSigilEmptyTry & 0x7)); // for compatibility w/ "packed"

  // The setup of `bits_t` is a bit subtle, see `rich_exception_ptr.md`.  This
  // lets us turn the bottom 3 alignment bits of the pointer into a union-type
  // selector, letting one pointer represent:
  //   - `exception_ptr`
  //   - Small values
  //   - An empty `Try`
  //   - Immortal `rich_error`s (think error codes + strings)
  //
  // These also support a few fast non-RTTI type queries.  Is this
  // `exception_ptr` known either to contain, or not to contain, a fast-path
  // type like cancellation or `rich_error`?
  //
  // The bit names have lowercase suffixes / prefixes that show how they're
  // meant to be used in tests:
  //   (FOO_and & bits_)
  //   FOO_eq == bits_
  //   FOO_masked_eq = (bits_ & mask_BAR)
  //
  // CRITICAL:
  // CRITICAL: DO NOT use values > 7 in the enum!
  // CRITICAL:
  friend constexpr bool test_rich_exception_ptr_bits();
  enum bits_t : uintptr_t {
    // If this bit 1, the union is an `std::exception_ptr` we own and must
    // destroy.  Its type is described by `mask_FAST_PATH_TYPES` bits.
    OWNS_EXCEPTION_PTR_and = 1,

    // These bits means empty `exception_ptr` when `immortal_storage_` is null.
    IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq = 0,
    // Future small-value optimization.  The idea is to inline `result<T>` for
    // trivial `T` of <= 61 bits modulo alignment, like `int` or `void&`.
    SMALL_VALUE_eq = 6,
    // When `rich_exception_ptr` is used in the `Try` implementation, the
    // "empty Try" state is represented by `kSigilEmptyTry` in `...sigil_`.
    //
    // Future: This can be extended to support other sigils, though keep in
    // mind the cost of branching over a large number of these.
    SIGIL_eq = 2,
    // `...eptr_ref_guard_` contains an UNOWNED copy of a leaky singleton of
    // `StoppedNoThrow`, which propagates through coros without
    // throwing, or being interrupted by `co_awaitTry`.
    NOTHROW_OPERATION_CANCELLED_eq = 4,

    // This mask applies to all subsequent `..._masked_eq` bit values.
    //
    // The use-case is fast, RTTI-free lookup for `get_exception<T>()` for a
    // few "fast-path" exception types.
    mask_FAST_PATH_TYPES = 6,

    // (1) and (2) two are valid regardless of `OWNS_EXCEPTION_PTR`
    //
    // They both require that the exception type be statically known when the
    // `rich_exception_ptr` is constructed.

    // (1) `IS_RICH_ERROR_BASE_masked_eq` means that pointed-to exception has
    // `rich_error_base` at offset 0, asserted via `has_offset0_base`.
    //
    // This bit value is 0 so that when we put `bits_t` in the lower 3 bits of
    // the pointer, `rich_exception_ptr` remains `constexpr` -- in C++20,
    // `constexpr` can only create aligned pointers.
    //
    // There's an implementation CAVEAT explaining why we don't ingest, for
    // example, `unique_ptr<Ex>`.  If a user class derived both from
    // `rich_error_base` and `T`, then an instance pointer could theoretically
    // be sliced to `T*` before we see it.  If that happened, fast-path
    // `get_exception<rich_error_base>()` would give a different answer than
    // RTTI.  Fortunately:
    //   - By-value construction of `rich_exception_ptr` is safe, and
    //   - `immortal_rich_error_t` also avoids the risk by using a non-type
    //     template parameter.
    IS_RICH_ERROR_BASE_masked_eq = 0,
    // (2) Fast path for `get_exception<OperationCancelled>()`.  Its fast-path
    // bits deliberately match `NOTHROW_OPERATION_CANCELLED_eq`.  See the notes
    // in `rich_exception_ptr.md` to learn why & how we store an instance
    // pointer along with the type bits.  There is no object slicing risk --
    // users cannot extend this hierarchy.
    IS_OPERATION_CANCELLED_masked_eq = 4,

    // (3) and (4) are only used if `OWNS_EXCEPTION_PTR` is set.

    // (3) Fast negative for the above optimized `get_exception<>()` calls.
    // Also requires the exception type to be statically known.
    owned_eptr_KNOWN_NON_FAST_PATH_TYPE_masked_eq = 6,
    // (4) Slow path: needs RTTI
    owned_eptr_UNKNOWN_TYPE_masked_eq = 2,
  };

  // `FOLLY_SAFE_DCHECK` doesn't work in `constexpr`, so we have `debug_assert`.
#ifdef NDEBUG
  FOLLY_ALWAYS_INLINE static void debug_assert(const char*, bool) {}
#else
  static void debug_assert(const char*, bool);
#endif
  // Since it's UB to throw an empty eptr, we terminate instead
  [[noreturn]] static void terminate_on_empty_or_invalid_eptr(bits_t);

  static bool bits_store_eptr(bits_t bits) {
    return (OWNS_EXCEPTION_PTR_and & bits) ||
        NOTHROW_OPERATION_CANCELLED_eq == bits;
  }
};
// All storage is private to `rich_exception_ptr_{packed,separate}_storage`.
// This enforces information hiding -- all data / bit access must use the API.
static_assert(std::is_empty_v<rich_exception_ptr_base_storage>);

// "Separate" and "packed" storage accessors must:
//   - behave uniformly across "separate" and "packed" storage,
//   - check the bits are in an appropriate state, when possible,
//   - validate that potentially-overlapping data correctly clears the low bits
//     that "packed" needs to use.
//
// Yes, both storage classes are constructed uninitialized. That's by design.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
class rich_exception_ptr_separate_storage
    : public rich_exception_ptr_base_storage {
 private:
  data_t data_;
  bits_t bits_;

 protected:
  constexpr void apply_bits_after_setting_data_with(auto data_fn, bits_t bits) {
    data_fn(data_);
    if (std::is_constant_evaluated()) {
      if (bits) {
        // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
        throw "Expect bits to be zero in consteval code";
      }
    } else {
      debug_assert("separate apply_bits data", !(data_.uintptr_ & 0x7));
      debug_assert("separate apply_bits bits", bits <= 0x7);
    }
    bits_ = bits;
  }

  constexpr bits_t get_bits() const {
    if (std::is_constant_evaluated() && bits_ != 0) {
      // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
      throw "Expect bits to be zero in consteval code";
    }
    return bits_;
  }

  // CAREFUL: Lacks a bits `debug_assert` because its 2 callsites have one.
  constexpr const detail::immortal_exception_storage*
  get_immortal_storage_or_punned_uintptr() const {
    return data_.immortal_storage_;
  }

  uintptr_t get_uintptr() const {
    debug_assert(
        "get_uintptr separate",
        SIGIL_eq == get_bits() || SMALL_VALUE_eq == get_bits());
    return data_.uintptr_;
  }

  const const_eptr_ref_guard& get_eptr_ref_guard() const {
    debug_assert("separate get_eptr_ref_guard", bits_store_eptr(get_bits()));
    return data_.eptr_ref_guard_;
  }
  mutable_eptr_ref_guard& get_eptr_ref_guard() {
    debug_assert("separate get_eptr_ref_guard", bits_store_eptr(get_bits()));
    return data_.eptr_ref_guard_;
  }
};
static_assert(
    sizeof(rich_exception_ptr_separate_storage) ==
    sizeof(std::exception_ptr) + sizeof(uintptr_t));

inline constexpr int min_alignment_for_packed_rich_exception_ptr = 8; // 3 bits

// Storage contract documented on `rich_exception_ptr_separate_storage`.
//
// Check `is_supported` before using "packed" storage!  It will hide union bits
// in the low 3 bits of the first `uintptr_t` of the data, so those MUST be
// guaranteed to be empty. Here are the criteria from `is_supported`:
//   - When the union stores `immortal_exception_storage*`, check the
//     alignment of that stored type.
//   - When the union stores `std::exception_ptr`, we have to rely on
//     platform implementation details.  On all supported platforms, the
//     first `uintptr_t` of the eptr is a pointer.  On Itanium ABI platforms,
//     `__cxa_allocate_exception` only takes `thrown_size`, so it necessarily
//     has to store the exception with the maximum fundamental alignment.  On
//     Windows, the alignment situation is not documented, but it would be
//     quite broken if it didn't at least provide alignment suitable for
//     `std::exception`, so we test that.
//
// Future: If we have to support more than libgcc/libstdc++/MSVC, this might
// merit some allow-list-by-platform conditional compilation.
class rich_exception_ptr_packed_storage
    : public rich_exception_ptr_base_storage {
 private:
  data_t data_;

 public:
  static constexpr bool is_supported = // Explained above
      alignof(detail::immortal_exception_storage) >=
          min_alignment_for_packed_rich_exception_ptr &&
      alignof(std::max_align_t) >=
          min_alignment_for_packed_rich_exception_ptr &&
      alignof(std::exception) >= min_alignment_for_packed_rich_exception_ptr;

 protected:
  template <typename T = rich_exception_ptr_packed_storage>
    requires T::is_supported
  constexpr void apply_bits_after_setting_data_with(auto data_fn, bits_t bits) {
    data_fn(data_);
    if (std::is_constant_evaluated()) {
      if (bits) {
        // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
        throw "Expect bits to be zero in consteval code";
      }
    } else {
      debug_assert("packed apply_bits data", !(data_.uintptr_ & 0x7));
      debug_assert("packed apply_bits bits", bits <= 0x7);
      data_.uintptr_ = data_.uintptr_ | bits;
    }
  }

  constexpr bits_t get_bits() const {
    if (std::is_constant_evaluated()) {
      // Consteval code paths cannot get bit-level access to pointers, so we
      // must assume 0.  `rich_exception_ptr_separate_storage::get_bits` and
      // `rich_exception_ptr_packed_storage::apply_bits...` assert this.
      return static_cast<bits_t>(0);
    }
    return static_cast<bits_t>(data_.uintptr_ & 0x7);
  }

  // CAREFUL: Lacks a bits `debug_assert` because its 2 callsites have one.
  constexpr const detail::immortal_exception_storage*
  get_immortal_storage_or_punned_uintptr() const {
    return data_.immortal_storage_;
  }

  uintptr_t get_uintptr() const {
    debug_assert(
        "get_uintptr packed",
        SIGIL_eq == get_bits() || SMALL_VALUE_eq == get_bits());
    return (~(uintptr_t)0x7) & data_.uintptr_;
  }

 private:
  mutable_eptr_ref_guard get_eptr_ref_guard_impl() const {
    debug_assert("packed get_eptr_ref_guard", bits_store_eptr(get_bits()));
    mutable_eptr_ref_guard ep_guard = data_.eptr_ref_guard_;
    ep_guard.uintptrs_[0] = ~(uintptr_t)0x7 & ep_guard.uintptrs_[0];
    return ep_guard;
  }

 protected:
  const_eptr_ref_guard get_eptr_ref_guard() const {
    return get_eptr_ref_guard_impl();
  }
  mutable_eptr_ref_guard get_eptr_ref_guard() {
    return get_eptr_ref_guard_impl();
  }
};
static_assert(
    sizeof(rich_exception_ptr_packed_storage) == sizeof(std::exception_ptr));

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
