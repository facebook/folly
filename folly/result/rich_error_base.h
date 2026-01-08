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

#include <folly/CppAttributes.h>
#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/Traits.h>
#include <folly/Utility.h> // FOLLY_DECLVAL
#include <folly/portability/SourceLocation.h>
#include <folly/result/rich_error_fwd.h>

#if FOLLY_HAS_RESULT

namespace folly {

namespace detail {
class enriched_non_value;
template <typename>
class immortal_rich_error_storage;
template <typename>
class rich_error_test_for_partial_message;
template <typename, typename>
class rich_exception_ptr_impl;
} // namespace detail

// As per `docs/rich_error.md`, user rich error types should use this alias to
// specify fast exception lookup hints.  This alias is preferred to `tag_t<...>`
// for brevity, clarity, and forward-compatibility with future optimizations.
//
// This takes a pack because a base type may want to hint at the likely derived
// types.  Hints are checked linearly left-to-right, so keep the list short (<
// 5), or performance may degrade to be no-better-than-RTTI.
//
//   struct Base : rich_error_base {
//     // hint the base last, since it is rarely used directly
//     using folly_get_exception_hint_types = rich_error_hints<Derived, Base>;
//   };
//   struct Derived : Base {
//     using folly_get_exception_hint_types = rich_error_hints<Derived>;
//   };
//
// Implementation note: Do not hint the bare `ThisAndLikelyDerivedTypes`, or
// `immortal_rich_error_storage<...>`, since we never make
// `std::exception_ptr`s with either of those, only with `rich_error<...>`.
template <typename... ThisAndLikelyDerivedTypes>
using rich_error_hints = tag_t<rich_error<ThisAndLikelyDerivedTypes>...>;

/// The main API for `rich_error<...>` types.  Prefer to use `get_rich_error()`
/// to get pointers to this interface -- its "happy path" avoids RTTI costs.
/// Or, when `rich_error.h` isn't included, `get_exception<rich_error_base>()`.
///
/// To define a rich error type `T`, derive from `rich_error_base` or a
/// descendant, and add:
///   using folly_get_exception_hint_types = rich_error_hints<T>;
///
/// Then, construct instances via `rich_error<T>` or `immortal_rich_error<T,
/// ...>`. By convention, most errors provide `rich_error<T> T::make()` static
/// factories.
///
/// `coded_rich_error.h` is like `std::system_error`, but cheaper and with more
/// functionality.  It stores a user-specified code & message, and may nest
/// other "caused-by" exceptions underneath itself.
///
/// `underlying_error()` is non-virtual to speed up error checks --
/// `get_exception<Ex>(rich_exception_ptr)` skips to the "underlying" error.
/// This sped up almost all affected `rich_exception_ptr` benchmarks.
class rich_error_base {
 private:
  template <typename>
  friend class rich_error; // may instantiate us
  template <typename>
  friend class detail::immortal_rich_error_storage; // may instantiate us
  template <typename>
  friend class detail::rich_error_test_for_partial_message; // test trait

  // The member forces all rich errors to be instantiated through one of these
  // "leaf" error objects:
  //   - `rich_error<UserBase>`
  //   - `immortal_rich_error<UserBase, ...>`
  //
  // This is important for several reasons:
  //
  //   - Consistent UX: both `get_exception<rich_error<UserBase>>(rep)` and
  //     `get_exception<UserBase>(rep)` should work, regardless of whether the
  //     `rich_exception_ptr` points at a dynamic or an immortal error.  The
  //     catch is that immortals must be constexpr, but `std::exception` is not
  //     `constexpr` until C++26.  Adding it as a base of the leaf
  //     `rich_error<UserBase>` works around this issue.
  //
  //   - Object-slicing safety, while letting rich errors be movable.  Compared
  //     to in-place, constructing regular classes is simple.  Movability keeps
  //     costs low -- e.g. it avoids `exception_shared_string` atomic ops.
  //
  //     Here is a canonical slicing bug: moving a base-class subobject from a
  //     derived instance, which most likely invalidates the latter.  The
  //     derived-class state may have depended on the base-class subobject
  //     state and is not prepared for it to have been stolen.
  //
  //   - As a bonus, this lets us verify `folly_get_exception_hint_types`
  //     for all rich errors.  Today, this helps maintain a consistently fast
  //     "happy path" lookup performance.  In the future, this hook can help
  //     implement fast RTTI queries as in `docs/future_fast_rtti.md`.
  //
  // The passkey stops derived classes from overriding the private member.
  struct only_rich_error_may_instantiate_t {};
  virtual void only_rich_error_may_instantiate(
      only_rich_error_may_instantiate_t) = 0;

  // This is set only by `enriched_non_value`.  All reads should go through
  // `...underlying_error()` to make risky mutable access more obvious.
  //
  // It is accessed on each `get_exception<Ex>(rich_exception_ptr)`, and by
  // member functions that use the traversal `get_underlying()`.  There's a
  // noticeable speedup from avoiding a vtable dispatch for this.  It's
  // especially noticeable in `get_rich_error()`, saving as much as 50% for
  // dynamic, known-`rich_exception_base` pointers.  There are smaller but still
  // noticeable gains throughout the benchmark.
  rich_exception_ptr* underlying_ptr_{};

 protected: // Can only construct via `rich_error<>` / `immortal_rich_error<>`
  constexpr rich_error_base() = default;

  rich_error_base(const rich_error_base&) = default;
  rich_error_base(rich_error_base&&) = default;
  rich_error_base& operator=(const rich_error_base&) = default;
  rich_error_base& operator=(rich_error_base&&) = default;

 public:
  virtual ~rich_error_base() = default;

  virtual folly::source_location source_location() const noexcept;

  // Use `<<` or `fmt` to log errors, AVOID `partial_message()` & `what()`.
  //   - `<<` and `fmt` render more information than this partial message:
  //     enrichment info, exception-specific data, source location.
  //   - With care, you can avoid heap allocations for the complete message.
  //
  // Most higher-level errors in `folly/result/` implement this for you -- see
  // e.g. `coded_rich_error` and `enrich_non_value.h`. If no base supplies it,
  // `rich_error` and `immortal_rich_error` will automatically back-fill this
  // with `pretty_name` of the base.
  //
  // `rich_error` implements `what()` in terms of `partial_message()`.
  // An implementation needing a dynamic `what()` should strongly consider
  // `exception_shared_string` as storage, since that efficiently stores a
  // string literal pointer OR a heap-allocated refcounted string.
  //
  // Design note: This only exists to implement `std::exception::what()`. That
  // unfortunate API forces one of several poor choices:
  //  - Only support literal string messages.
  //  - Use a dynamic allocation to eagerly pre-format the full message when
  //    the error happens, whether it'll be used or not.
  //  - Do some atomic trickery to make the allocation & format step lazy.
  // In contrast, rich error `fmt` support is lazy & costs nothing up-front.
  // Storing only structured data & literal strings, they are cheap to make.
  virtual const char* partial_message() const noexcept = 0;

  // Formatting of rich errors follows the `next_error_for_enriched_message()`
  // linked list, printing each one in turn.  There are two use-cases:
  //
  // (1) `enrich_non_value` -- as you stack these, the outer one always points
  // at the next one, etc.  But the `underlying_error()` for all of them points
  // at the original error being propagated. The log output will be like this:
  //
  //   OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
  //
  // Here, "via" means "what follows is an enrichment stack for the error", and
  // "after" separates entries in that stack.
  //
  // (2) To emulate `std::nested_exception` (but much cheaper to log!), any
  // rich error type may internally store `rich_exception_ptr next_`, and
  // expose that via `next_error_for_enriched_message()`.  An example is
  // provided in `nestable_coded_rich_error.h`.  For example, if `OriginalErr`
  // from (1) had wrapped `NestedErr`, which was turn wrapped by its own
  // `enrich_non_value` during propagation, then we might see this output:
  //
  //   OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
  //   [after] NestedErr [via] nested_src.cpp:12
  virtual const rich_exception_ptr* next_error_for_enriched_message()
      const noexcept;

  // Used only by "transparent" error wrappers like `enrich_non_value()`.
  // Otherwise, `nullptr`, meaning that `this` itself is the underlying error.
  //
  // From a program-logic perspective, `underlying_error()` is the error that
  // is actually propagating.  To observe anything about `this`, the outer
  // error object, the end-user would have to call `get_outer_exception`.
  //
  // Every enrichment wrapper points this at the original error, so that
  // `get_exception<Ex>()` is O(1).
  constexpr const rich_exception_ptr* underlying_error() const noexcept {
    return underlying_ptr_;
  }
  class underlying_error_private_t {
    friend class detail::enriched_non_value; // Sets `underlying_ptr_`
    // Needs mutable access for `get_mutable_exception`.
    template <typename, typename>
    friend class detail::rich_exception_ptr_impl;
    underlying_error_private_t() = default;
  };
  // This is private because allowing mutable access to the `underlying_ptr_`
  // is very risky.  It is only used to implement `get_mutable_exception`,
  // which does NOT mutate the underlying REP, but only the pointed-to
  // exception object.  This distinction is important for wrappers because
  // `*underlying_ptr_` aka `enriched_non_value::next_` ends up being
  // unexpectectedly shared state.  Consider:
  //   - `repConst` and `repMutable` both point to an `enriched_non_value`
  //     object, call it `e`.  It sets `underlying_ptr_` to point to its
  //     `next_` member.
  //   - A function calls `repMutable.with_underlying()` or
  //     `mutable_underlying_error()` and gets `e.next_`.
  //   - If that is a non-`const` pointer, the function can now mutate the
  //     pointed-to `rich_exception_ptr`, and suddenly the underlying error
  //     object of `repConst` also changes -- but the identity of a wrapped
  //     error object should NOT change!
  // Gate this API since this kind of aliasing bug is subtle AND dangerous.
  constexpr rich_exception_ptr* mutable_underlying_error(
      underlying_error_private_t) noexcept {
    return underlying_ptr_;
  }

 protected:
  // Only used by `enriched_non_value`.  It can't set `underlying_ptr_` until
  // AFTER its `next_` is populated, so this is a setter, not a ctor argument.
  // Do NOT add more callsites without maintainer review -- the safer design
  // might be to add an immovable base class that exposes the setter.
  void set_underlying_error(
      underlying_error_private_t, rich_exception_ptr* ptr) noexcept {
    underlying_ptr_ = ptr;
  }
};

} // namespace folly

namespace folly::detail {

// Implementation notes:
//  - This needs a body because GCC doesn't want `d` referenced in a `->` type
//    signature.
//  - To use this with the cheaper-to-compile `FOLLY_DECLVAL`, which is
//    `nullptr`, this must be in an unevaluated context, since patently-null
//    static casts are special in that they discard offsets.  So, the below
//    equality would always be true during constant evaluation.
template <typename B, typename D>
inline consteval auto is_offset0_base_of(D d) {
  return std::bool_constant<
      static_cast<const void*>(&d) ==
      static_cast<const void*>(static_cast<const B*>(&d))>{};
}

// Has `test_has_offset0_base()` in `rich_error_test.cpp`.
//
// This looks superficially similar to `is_pointer_interconvertible_base_of_v`,
// but they differ for multiple & virtual inheritance:
// https://godbolt.org/z/P8Teoh1zh
//
// Future: Similar to `promise_at_offset0`, worth unifying?
template <typename D, typename B>
concept has_offset0_base =
    decltype(is_offset0_base_of<B>(FOLLY_DECLVAL(D)))::value;

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
