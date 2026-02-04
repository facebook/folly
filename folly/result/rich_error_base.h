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
#include <folly/Unit.h>
#include <folly/Utility.h> // FOLLY_DECLVAL
#include <folly/lang/Pretty.h>
#include <folly/portability/SourceLocation.h>
#include <folly/result/rich_error_fwd.h>

#include <iosfwd>
#include <iterator>
#include <fmt/core.h>

// Old `fmt` isn't actually supported, but ... it needs to build :(
#if FMT_VERSION < 80000
namespace fmt {
using appender = std::back_insert_iterator<fmt::internal::buffer<char>>;
template <typename T>
struct is_formattable;
} // namespace fmt
#endif

#if FOLLY_HAS_RESULT

namespace folly {

class rich_error_code_query;

namespace detail {
class epitaph_non_value;
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

  // This is set only by `epitaph_non_value`.  All reads should go through
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
  //     epitaphs, exception-specific data, source location.
  //   - With care, you can avoid heap allocations for the complete message.
  //
  // Most higher-level errors in `folly/result/` implement this for you -- see
  // e.g. `coded_rich_error` and `epitaph.h`. If no base supplies it,
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

  // Override this to support RTTI-free `get_rich_error_code()`. See the
  // `rich_error_bases_and_own_codes` doc for how to implement this.
  //
  // Briefly: The query contains an ABI-stable UUID for a rich error code type
  // `C`.  If the error has a code of that type, this call undoes the type
  // erasure.  The returned query is then updated so the user can efficiently
  // get the value type `C`.  There's also a special `kFormatterUuid`, which
  // instead returns a callback that formats ALL the codes of this error.
  constexpr virtual void retrieve_code(rich_error_code_query&) const {}

  // Returns a `fmt`-formattable object that renders all the available codes in
  // this error's inheritance hierarchy, like so: "Specific=1, General=2".
  //
  //   const rich_error_base& err = ...;
  //   auto s = fmt::format("code={}", err.all_codes_for_fmt());
  class fmt_all_codes_t {
   private:
    friend class rich_error_base;
    friend struct fmt::formatter<folly::rich_error_base::fmt_all_codes_t>;
    const rich_error_base& error_ref_;
    const char* pre_separator_;
    bool saw_code_{false}; // formatted at least one code?
    constexpr fmt_all_codes_t(
        const rich_error_base& e [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]],
        const char* pre_separator)
        : error_ref_(e), pre_separator_(pre_separator) {}
  };
  fmt_all_codes_t all_codes_for_fmt(const char* pre_separator = "") const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return fmt_all_codes_t{*this, pre_separator};
  }

  // Rendering for rich errors via `fmt` and `ostream<<`.
  virtual void format_to(fmt::appender& out) const;

  // Format this epitaph stack, starting with its underlying error.
  void format_with_epitaphs(fmt::appender& out) const;

  // Format only the epitaph stack, omitting the first underlying error.
  // Precondition: `this` is a wrapper, not an underlying error.
  void format_with_epitaphs_without_first_underlying(fmt::appender& out) const;

  // Formatting of rich errors follows the `next_error_for_epitaph()`
  // linked list, printing each one in turn.  There are two use-cases:
  //
  // (1) `epitaph` -- as you stack these, the outer one always points
  // at the next one, etc.  But the `underlying_error()` for all of them points
  // at the original error being propagated. The log output will be like this:
  //
  //   OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
  //
  // Here, "via" means "what follows is an epitaph stack for the error", and
  // "after" separates entries in that stack.
  //
  // (2) To emulate `std::nested_exception` (but much cheaper to log!), any
  // rich error type may internally store `rich_exception_ptr next_`, and
  // expose that via `next_error_for_epitaph()`.  An example is
  // provided in `nestable_coded_rich_error.h`.  For example, if `OriginalErr`
  // from (1) had wrapped `NestedErr`, which was turn wrapped by its own
  // `epitaph` during propagation, then we might see this output:
  //
  //   OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
  //   [after] NestedErr [via] nested_src.cpp:12
  virtual const rich_exception_ptr* next_error_for_epitaph() const noexcept;

  // Used only by "transparent" error wrappers like `epitaph()`.
  // Otherwise, `nullptr`, meaning that `this` itself is the underlying error.
  //
  // From a program-logic perspective, `underlying_error()` is the error that
  // is actually propagating.  To observe anything about `this`, the outer
  // error object, the end-user would have to call `get_outer_exception`.
  //
  // Every epitaph wrapper points this at the original error, so that
  // `get_exception<Ex>()` is O(1).
  constexpr const rich_exception_ptr* underlying_error() const noexcept {
    return underlying_ptr_;
  }
  class underlying_error_private_t {
    friend class detail::epitaph_non_value; // Sets `underlying_ptr_`
    // Needs mutable access for `get_mutable_exception`.
    template <typename, typename>
    friend class detail::rich_exception_ptr_impl;
    underlying_error_private_t() = default;
  };
  // This is private because allowing mutable access to the `underlying_ptr_`
  // is very risky.  It is only used to implement `get_mutable_exception`,
  // which does NOT mutate the underlying REP, but only the pointed-to
  // exception object.  This distinction is important for wrappers because
  // `*underlying_ptr_` aka `epitaph_non_value::next_` ends up being
  // unexpectectedly shared state.  Consider:
  //   - `repConst` and `repMutable` both point to an `epitaph_non_value`
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
  // Only used by `epitaph_non_value`.  It can't set `underlying_ptr_` until
  // AFTER its `next_` is populated, so this is a setter, not a ctor argument.
  // Do NOT add more callsites without maintainer review -- the safer design
  // might be to add an immovable base class that exposes the setter.
  void set_underlying_error(
      underlying_error_private_t, rich_exception_ptr* ptr) noexcept {
    underlying_ptr_ = ptr;
  }
};

/// `rich_error_base` is `fmt` formattable (below), but also has this sugar for
/// writing rich errors to glog & `std` streams.  This ought to be more robust
/// under OOM than `stream << fmt::format("{}", err)`, since `fmt` may allocate.
std::ostream& operator<<(std::ostream&, const rich_error_base&);

template <typename Ex>
class rich_ptr_to_underlying_error;

namespace detail {
template <auto, typename Ex>
void expectGetExceptionResult(const rich_ptr_to_underlying_error<Ex>&);
} // namespace detail

// Quacks like `Ex*`, but show an epitaph stack when formatted.
// Returned by `get_exception<Ex>(rep)` and `get_mutable_exception<Ex>(rep)`,
// for `rich_exception_ptr<...> rep`.  The respective `get_exception()`
// implementations have more detailed docs.
template <typename Ex>
class rich_ptr_to_underlying_error {
 private:
  Ex* raw_ptr_{nullptr};
  const rich_error_base* top_rich_error_{nullptr};

  friend struct fmt::formatter<rich_ptr_to_underlying_error>;
  template <auto, typename T>
  friend void detail::expectGetExceptionResult(
      const rich_ptr_to_underlying_error<T>&);

 protected:
  template <typename, typename>
  friend class detail::rich_exception_ptr_impl;
  constexpr rich_ptr_to_underlying_error(Ex* p, const rich_error_base* top)
      : raw_ptr_(p), top_rich_error_(top) {}

 public:
  // `get_exception<Ex>(result)` needs this.
  explicit constexpr rich_ptr_to_underlying_error(std::nullptr_t) {}

  // Immovable for now, since the primary use-case is just:
  //   if (auto ex = get_exception<Ex>(rich_eptr)) { /*...*/ }
  // Escape hatch ideas:
  //   - `Ex* get()` below
  //   - to delegate formatting to helper func, pass by `auto&`
  //
  // Future: Relax this if you have a compelling reason.  Some redundant
  // safety comes from the `lifetimebound` annotation on `get_exception`.
  // But, in clang-17 that doesn't catch some obvious use-after-frees.
  rich_ptr_to_underlying_error(const rich_ptr_to_underlying_error&) = delete;
  rich_ptr_to_underlying_error operator=(const rich_ptr_to_underlying_error&) =
      delete;
  rich_ptr_to_underlying_error(rich_ptr_to_underlying_error&&) = delete;
  rich_ptr_to_underlying_error operator=(rich_ptr_to_underlying_error&&) =
      delete;
  ~rich_ptr_to_underlying_error() = default;

  constexpr Ex& operator*() const { return *raw_ptr_; }
  constexpr Ex* operator->() const { return raw_ptr_; }
  constexpr Ex* get() const { return raw_ptr_; }

  // Conversion to raw pointer lossy.  Make it explicit to avoid accidentally
  // shedding rich error formatting context -- propagation notes, source
  // locations, codes, etc.
  explicit constexpr operator Ex*() const { return raw_ptr_; }

  // Make `if (auto ex = get_exception<...>(...))` work.  Or, use `bool{ex}`
  // for explicit conversion.  Truly quacking like a raw pointer would make
  // this implicit, but that has undesirable consequences.  For example, any
  // common protocol that takes `bool` like `fmt` or `<<(ostream&, bool)` would
  // treat these as `bool`, unless a more specific match is provided.  In the
  // future, we could reconsider this trade-off.
  explicit constexpr operator bool() const { return raw_ptr_; }

  friend constexpr bool operator==(
      std::nullptr_t, const rich_ptr_to_underlying_error& p) {
    return p.raw_ptr_ == nullptr;
  }
  friend constexpr bool operator==(
      const Ex* raw_p, const rich_ptr_to_underlying_error& p) {
    return raw_p == p.raw_ptr_;
  }
  friend constexpr bool operator==(
      const rich_ptr_to_underlying_error& lhs,
      const rich_ptr_to_underlying_error& rhs) {
    return lhs.raw_ptr_ == rhs.raw_ptr_;
  }
};

// See docs in `rich_error_code.h`
//
// DO NOT add your own `rich_error_code<std::errc>` -- use `errc_rich_error.h`.
template <typename Code>
struct rich_error_code;

// User-opaque part of the `get_rich_error_code()` machinery. End-users only
// pass it from their `retrieve_code()` override into the `retrieve_code` impl.
//
// Implementation note -- this has 3 roles:
//  - Forwarding the UUID of the desired `Code` into `retrieve_code()`.
//  - Stores a nullable, type-erased `Code` for `get_rich_error_code()`.
//  - Passkey privacy -- ensures the only public entry point to the rich error
//    code machinery is `get_rich_error_code()`.
class rich_error_code_query {
 private:
  uint64_t uuid_;

 protected:
  // Stores either a mangled code or a formatter function pointer.
  // The active member depends on the `uuid_` value:
  //   Non-reserved: mangled_code_ is active (normal code retrieval)
  //   kFormatterUuid: formatter_fn_ is active (formatting all codes)
  union {
    uintptr_t mangled_code_;
    bool (*formatter_fn_)( // Returns `true` if > 0 codes were formatted
        const rich_error_base&, fmt::appender&, const char* pre_separator);
  };

  // Future: This should use `std::optional`, but we still need this to build
  // with libstdc++ from GCC 11.2, which lacks a constexpr `emplace`.
  // https://godbolt.org/z/Y77WfKG94
  bool has_value_{false};

  // Reserved UUIDs: [0, 100000]
  static constexpr uint64_t kFormatterUuid = 0; // `fmt` formatting
  static constexpr uint64_t kMaxReservedUuid = 100000;

  // We don't want to initialize the union data guarded by `has_value_`.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  explicit constexpr rich_error_code_query(uint64_t uuid) : uuid_{uuid} {}

  constexpr bool uuid_matches(uint64_t uuid) const { return uuid_ == uuid; }

  template <typename, typename, auto...>
  friend class rich_error_bases_and_own_codes;

  friend struct fmt::formatter<rich_error_base::fmt_all_codes_t>;

  template <typename Code>
  friend class get_rich_error_code_fn;

 public:
  // Required for usage where an outer class delegates code retrieval to an
  // inner one, and provides one in case of miss.
  bool has_value() const { return has_value_; }
};

} // namespace folly

// `rich_error_base` AND derived classes are formattable.
template <>
struct fmt::formatter<folly::rich_error_base> {
  constexpr format_parse_context::iterator parse(format_parse_context& ctx) {
    // FIXME: We don't currently support align/fill/padding, not because it's
    // impossible, but because `fmt` didn't make it easy -- all the code
    // related to spec handling, and padded/filled/aligned output of strings is
    // in `fmt::detail`.  When we do, don't use `fmt::nested_formatter` (this
    // allocates, which adds unnecessary fragility on an error handling path).
    // Instead, add a `rich_error_base::formatted_size()` and use that.
    return ctx.begin();
  }
  format_context::iterator format(
      const folly::rich_error_base& e, format_context& ctx) const {
    auto it = ctx.out();
    e.format_with_epitaphs(it);
    return it;
  }
};
template <typename T>
  requires std::is_convertible_v<T*, folly::rich_error_base*>
struct fmt::formatter<T> : fmt::formatter<folly::rich_error_base> {};

// Format pointer-like returned by `get_exception<Ex>(rich_exception_ptr)`.
// Crucially, this displays `epitaph()` stacks when available.
//
template <typename Ex>
struct fmt::formatter<folly::rich_ptr_to_underlying_error<Ex>> {
 private:
  // `Ex` is formattable AND not a `rich_error_base`: format `Ex` first, then
  // append the rich error formatting.
  //
  // Rather than branch a single class on this, it would be cleaner to use
  // multiple partial specializations with mutually-exclusive `requires`
  // clauses, but GCC treats those as redefinitions.
  static constexpr bool kUseExFormatter = fmt::is_formattable<Ex>::value &&
      !std::is_convertible_v<Ex*, const folly::rich_error_base*>;

  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] folly::conditional_t<
      kUseExFormatter,
      fmt::formatter<std::remove_cv_t<Ex>>,
      folly::Unit> ex_formatter_;

 public:
  constexpr format_parse_context::iterator parse(format_parse_context& ctx) {
    if constexpr (kUseExFormatter) {
      return ex_formatter_.parse(ctx);
    } else {
      return ctx.begin();
    }
  }

  format_context::iterator format(
      const folly::rich_ptr_to_underlying_error<Ex>& p,
      format_context& ctx) const {
    using UncvEx = std::remove_cv_t<Ex>;
    auto it = ctx.out();
    if (p.raw_ptr_ == nullptr) {
      fmt::format_to(it, "[nullptr folly::rich_ptr_to_underlying_error]");
      return it;
    }
    if constexpr (kUseExFormatter) {
      it = ex_formatter_.format(*p.raw_ptr_, ctx);
      if (p.top_rich_error_) {
        // Underlying error already formatted above, just format the wrappers
        p.top_rich_error_->format_with_epitaphs_without_first_underlying(it);
      }
      return it;
    } else {
      // Use detailed rich-error formatting if available: either we have a
      // epitaph wrapper, or the underlying error is rich, or both.
      if (std::is_convertible_v<Ex*, const folly::rich_error_base*> ||
          p.top_rich_error_) {
        p.top_rich_error_->format_with_epitaphs(it);
      } else {
        // For non-formattable non-rich errors without a wrapper, match the
        // `rich_error_base::format_to` formatting.
        if constexpr (std::is_convertible_v<Ex*, const std::exception*>) {
          fmt::format_to(
              it, "{}: {}", folly::pretty_name<UncvEx>(), p.raw_ptr_->what());
        } else {
          fmt::format_to(it, "{}", folly::pretty_name<UncvEx>());
        }
      }
      return it;
    }
  }
};

namespace folly {
namespace detail {
std::ostream& ostream_write_via_fmt(std::ostream& os, const auto& v) {
  try {
    os << fmt::format("{}", v);
  } catch (const std::bad_alloc&) {
    // Per `ostream_append_simple_rich_error`, ~4.5x slower than `os <<
    // fmt::format("{}", e);` for small strings, but doesn't use heap.
    fmt::format_to(std::ostream_iterator<char>(os), "{}", v);
  }
  return os;
}
} // namespace detail

template <typename Ex>
std::ostream& operator<<(
    std::ostream& os, const rich_ptr_to_underlying_error<Ex>& ep) {
  return detail::ostream_write_via_fmt(os, ep);
}
} // namespace folly

// See `rich_error_code::all_codes_for_fmt()` for usage
template <>
struct fmt::formatter<folly::rich_error_base::fmt_all_codes_t> {
  constexpr format_parse_context::iterator parse(format_parse_context& ctx) {
    return ctx.begin();
  }
  format_context::iterator format(
      folly::rich_error_base::fmt_all_codes_t& err, // this mutates `.saw_code_`
      format_context& ctx) const {
    folly::rich_error_code_query q{
        folly::rich_error_code_query::kFormatterUuid};
    err.error_ref_.retrieve_code(q);
    auto out = ctx.out();
    if (q.has_value_) {
      err.saw_code_ = q.formatter_fn_(err.error_ref_, out, err.pre_separator_);
    }
    return out;
  }
};

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
