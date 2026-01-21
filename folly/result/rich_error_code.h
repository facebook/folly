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

#include <optional>
#include <fmt/core.h>

#include <folly/lang/Pretty.h>
#include <folly/result/rich_error_base.h>

#include <folly/Portability.h> // FOLLY_HAS_RESULT

#if FOLLY_HAS_RESULT

/// This file provides `get_rich_error_code<Code>(container)` -- both the
/// generic behaviors, and the `retrieve_code` protocol from `rich_error_base`.
///
/// Read `docs/rich_error_code.md` for a user-centric introduction.  These
/// in-header docs focus on the details of using & extending the machinery.
///
/// ## How to register a new error code
///
/// To use `Code` with `get_rich_error_code<Code>()`, you must specialize
/// `rich_error_code<Code> with the following features.
///
/// WARNING: It is forbidden to specialize `rich_error_code` for `std` enums
/// outside of `folly/result` -- for an example, see `errc_rich_error.h`.
///
/// (1) Mandatory: Stable UUID.  This becomes part of the ABI, and must not
/// collide with other codes, so it's best to pick a 64-bit random number:
///
///   python3 -c 'import random;print(random.randint(0, 2**64 - 1))'
///   static constexpr uint64_t uuid = <random>ULL;
///
/// Using `Code`s with colliding UUIDs in the same error class will cause a
/// compile error, forcing an ABI break. UUIDs in [0, 100000] are reserved.
///
/// Rationale: UUIDs enable a much-faster-than-RTTI type comparison. This idea
/// originates in Niall Douglas's wg21.link/P1028.
///
/// (2) Encouraged: `fmt::formatter<Code>` specialization, as per `fmt` docs.
/// May omit if you're ok with enums being printed as the underlying type.
///
/// (3) Optional: Code name for `fmt` output. Defaults to `pretty_name<Code>`.
///
///   static constexpr const char* name = "Code";
///
/// (4) If your code is NOT an enum, you **must** define mangle/unmangle
/// functions. Be sure to add round-trip tests, and watch out for truncation and
/// signedness issues.  Add `static_assert`s for platform-specific assumptions.
///
///   static constexpr uintptr_t mangle_code(B1 code);
///   static constexpr B1 unmangle_code(uintptr_t value);

namespace folly {

namespace detail {

// Mangle a `Code` into a type-erased `uintptr_t`
template <typename Code>
constexpr uintptr_t mangle_rich_error_code(Code code) {
  // Enum codes don't need a user-provided mangle function
  if constexpr (std::is_enum_v<Code>) {
    auto underlying = to_underlying(code);
    static_assert(sizeof(underlying) <= sizeof(uintptr_t));
    // Test for enum conversion: https://godbolt.org/z/Yf8qPznaf
    return static_cast<uintptr_t>(underlying);
  } else {
    return rich_error_code<Code>::mangle_code(code);
  }
}

// Unmangle a type-erased `uintptr_t` back to a `Code`
template <typename Code>
constexpr Code unmangle_rich_error_code(uintptr_t value) {
  // Enum codes don't need a user-provided unmangle function
  if constexpr (std::is_enum_v<Code>) {
    using Underlying = std::underlying_type_t<Code>;
    static_assert(sizeof(Underlying) <= sizeof(uintptr_t));
    return static_cast<Code>(static_cast<Underlying>(value));
  } else {
    return rich_error_code<Code>::unmangle_code(value);
  }
}

void retrieve_rich_error_code_from_exception_ptr(
    const std::exception_ptr&, rich_error_code_query&);

// Customization point for `get_rich_error_code<Code>()`.
//
// INTERNAL USE ONLY - End-users should NEVER specialize this trait. Instead,
// implement the `folly::get_exception<Ex>(v)` protocol for your type.
//
// This keeps `rich_error_code.h` & `folly/lang/Exception.h` independent.
//
// `get_rich_error_code<Code>()` should work with various error containers
// (`exception_ptr`, `exception_wrapper`, `result`, `rich_exception_ptr`, etc).
// The uniform way to get `rich_error_base*` out of those is `get_exception`,
// but we don't want `rich_error_code.h` to include `Exception.h`, or vice
// versa, and we even want to avoid `Exception.h` -> `rich_error_base.h`.
//
// To break the cycle, we add a second -- and last -- specialization of this
// template in `Exception.h`.
//
// ### ODR SAFETY ###
//
// Do NOT add or change specializations of this template without reading!
//
// In general, there's an ODR risk with declaring the primary template in
// multiple headers, since different include paths could synthesize different
// specializations for the same type.
//
// However, this specific setup is fine because:
//   - This is not a user-specializable trait, but a `detail::`.
//   - This template is ONLY used in `rich_error_code.h`.
//   - `Exception.h` is the ONLY other file declaring or specializing this
//     template.  That specialization can only compile when matching types that
//     speak `get_exception`.  Its participation in ODR violations is not
//     likely -- all current (and likely future) such types include
//     `Exception.h`, so the compiler would see conflicting definitions.
//
// The present current file specializes for:
//   - `rich_error_base`, because `Exception.h` shouldn't include its header.
//   - `std::exception_ptr` because this file is the only way to use the
//     template, and defining this standard-type specialization here ensures
//     that any redefinition is a compile error.
template <typename T, typename /*SFINAE*/>
struct get_rich_error_code_traits;

// Special case: to avoid ODR issues, must be in this header (details above)
template <std::same_as<std::exception_ptr> T>
struct get_rich_error_code_traits<T, void> {
  static void retrieve_code(const T& t, rich_error_code_query& q) {
    retrieve_rich_error_code_from_exception_ptr(t, q);
  }
};

// Most `get_rich_error_code` variants will delegate to this branch.
template <std::derived_from<rich_error_base> T>
struct get_rich_error_code_traits<T, void> {
  static constexpr void retrieve_code(const T& t, rich_error_code_query& q) {
    // Here, we can know both `Code` and the error type `T`, so it'd be possible
    // to explicitly devirtualize the query.  But, if the compiler can see the
    // body of `retrieve_code`, then it is usually able to pick the right branch
    // based on the statically-known `q.uuid`.  So, the utility is limited.
    t.retrieve_code(q);
  }
};

} // namespace detail

/// Implements `retrieve_code()` for errors supporting `get_rich_error_code()`.
///
/// `CodeFns...` are member function pointers returning error codes belonging to
/// this class, and not inherited from any base class.
///
/// `BaseList` is a type-list of direct base error classes of `Self`.
///
/// IMPORTANT: If a base of the class is omitted from this list, its codes will
/// not be retrievable. We cannot detect this mistake without C++26 reflection.
///
/// Example:
///
///   struct error_B : error_A {
///     B1 b1() const { return b1_; }
///     B2 b2() const { return b2_; }
///     using folly_rich_error_codes_t = rich_error_bases_and_own_codes<
///         error_B, tag_t<error_A>, &error_B::b1, &error_B::b2>;
///     constexpr void retrieve_code(rich_error_code_query& q) const override {
///       // NB: If you don't need `constexpr`, then you might be able to reduce
///       // code size & improve build speed by delegating to a private member
///       // that moves this instantiation to your `.cpp` file.
///       return folly_rich_error_codes_t::retrieve_code(*this, q);
///     }
///   };
///
/// Future / perf: If you are able to inline lookup (rare!), and benefit from it
/// (even rarer!), AND want to avoid bloat from formatting code in headers, the
/// fix is to add an out-of-line `format_all_codes_recursive` proxy to your
/// class, and pass it via an `auto` template to `retrieve_code()`.
template <typename Self, typename BaseList, auto... CodeFns>
class rich_error_bases_and_own_codes {
 private:
  template <typename, typename, auto...>
  friend class rich_error_bases_and_own_codes;

  template <auto CodeFn>
  using code_type =
      std::remove_cvref_t<decltype((FOLLY_DECLVAL(const Self&).*CodeFn)())>;

  template <typename... Bases>
  static constexpr auto uuids_for_codes_of_bases_and_self(tag_t<Bases...>)
      -> value_list_concat_t<
          vtag_t,
          typename Bases::folly_rich_error_codes_t::all_uuids_t...,
          vtag_t<rich_error_code<code_type<CodeFns>>::uuid...>>;

  using all_uuids_t = decltype(uuids_for_codes_of_bases_and_self(BaseList{}));

  template <uint64_t... UUIDs>
  static constexpr bool have_duplicate_or_reserved_uuids(vtag_t<UUIDs...>) {
    constexpr uint64_t arr[] = {UUIDs...};
    for (size_t i = 0; i < sizeof...(UUIDs); ++i) {
      // UUIDs <= 100000 are reserved (e.g. for `fmt` formatting)
      if (arr[i] <= rich_error_code_query::kMaxReservedUuid) {
        return true;
      }
      for (size_t j = i + 1; j < sizeof...(UUIDs); ++j) {
        if (arr[i] == arr[j]) {
          return true;
        }
      }
    }
    return false;
  }

  static_assert(
      !have_duplicate_or_reserved_uuids(all_uuids_t{}),
      "The error hierarchy contains a duplicate UUID, or a code with a "
      "UUID with a reserved value of <= 100000.");

 public:
  // Retrieve code for a given UUID.
  //
  // Future: With C++26 reflection, we could figure out `Bases` from the type of
  // `self`, preventing the user error where the 2 diverge.
  template <std::same_as<Self> Self2> // prevent slicing
  static constexpr void retrieve_code(
      const Self2& self, rich_error_code_query& query) {
    // Violating this would break inheritance of codes from `Self`
    static_assert(
        std::is_same_v<
            rich_error_bases_and_own_codes,
            typename Self2::folly_rich_error_codes_t>);

    // First, check for user-defined codes from this class (not inherited)
    ([&]<auto CodeFn>(vtag_t<CodeFn>) {
      // Call the function whose `Code` matches the requested UUID.
      if (query.uuid_matches(rich_error_code<code_type<CodeFn>>::uuid)) {
        query.mangled_code_ = detail::mangle_rich_error_code((self.*CodeFn)());
        query.has_value_ = true;
        return true;
      }
      return false;
    }(vtag<CodeFns>) ||
     ...) ||
        // Then, delegate to base classes
        [&]<typename... Bases>(tag_t<Bases...>) {
          return (
              ([&]() {
                // Need both `Bases` due to a language/compiler quirk
                static_cast<const Bases&>(self).Bases::retrieve_code(query);
                return query.has_value_;
              }()) ||
              ...);
        }(BaseList{});

    // Special case: UUID requests a formatter function. This branch is last to
    // minimize the impact on the "fast path" of code retrieval.
    //
    // NB: The only way to avoid this extra branch on miss would be to force
    // all errors to implement both `retrieve_code` and another piece of
    // auto-synthesized boilerplate as below, and that didn't seem worth it.
    //   void format_codes(
    //       fmt::format_context&, const char* pre_separator) const override;
    if (query.uuid_matches(rich_error_code_query::kFormatterUuid)) {
      query.formatter_fn_ = format_all_codes_recursive;
      query.has_value_ = true;
      return;
    }
  }

 private:
  // Format all codes from this error and its bases
  static bool format_all_codes_recursive(
      const rich_error_base& err,
      fmt::appender& out,
      const char* pre_separator) {
    bool has_code = false;
    const Self& self = static_cast<const Self&>(err);
    // Format own codes
    (
        [&]<auto CodeFn>(vtag_t<CodeFn>) {
          has_code = true;
          fmt::format_to(out, "{}", pre_separator);
          pre_separator = ", ";
          auto code = (self.*CodeFn)();
          using Code = decltype(code);
          const char* name = []() {
            if constexpr (requires { rich_error_code<Code>::name; }) {
              return rich_error_code<Code>::name;
            } else {
              return pretty_name<Code>();
            }
          }();

          // Use `format_as_t` wrapper if provided, otherwise format directly.
          // This lets `rich_error_code<Code>` provide custom formatting without
          // specializing `fmt::formatter<Code>` (useful for standard types
          // where such a specialization could cause ODR issues).
          if constexpr (requires {
                          typename rich_error_code<Code>::format_as_t;
                        }) {
            using FormatAs = typename rich_error_code<Code>::format_as_t;
            static_assert(
                fmt::is_formattable<FormatAs>::value,
                "rich_error_code<Code>::format_as_t must be formattable");
            fmt::format_to(out, "{}={}", name, FormatAs{code});
          } else if constexpr (fmt::is_formattable<Code>::value) {
            fmt::format_to(out, "{}={}", name, code);
          } else {
            // While most error codes should be formattable, and display a real
            // message, it seems ok to allow lightweight enum-only codes where
            // code brevity is key. (`fmt` doesn't format scoped enums)
            static_assert(
                std::is_enum_v<Code>,
                "Code must be an enum or have a formatter");
            fmt::format_to(
                out,
                "{}={}",
                name,
                static_cast<std::underlying_type_t<Code>>(code));
          }
        }(vtag<CodeFns>),
        ...);

    // Then, delegate to base classes to format their codes
    [&]<typename... Bases>(tag_t<Bases...>) {
      (
          [&]() {
            rich_error_code_query q{rich_error_code_query::kFormatterUuid};
            static_cast<const Bases&>(self).Bases::retrieve_code(q);
            if (q.has_value_) {
              if (q.formatter_fn_(err, out, pre_separator)) {
                has_code = true;
              }
              pre_separator = ", ";
            }
          }(),
          ...);
    }(BaseList{});
    return has_code;
  }
};

/// Extracts a typed error code from various error containers.
///
/// Returns a `Code` if:
///   - The container holds an error derived from `rich_error_base`, AND
///   - That error class supports `Code` (via `rich_error_bases_and_own_codes`)
/// Otherwise, returns `std::nullopt`.
///
/// Supported containers:
///   - Direct `rich_error_base` references
///   - `std::exception_ptr` (via `exception_ptr_get_object_hint`)
///   - `exception_wrapper`, `rich_exception_ptr`, `result`, `non_value_result`,
///     once you include the respective header.
///
/// Example:
///   if (auto code = get_rich_error_code<MyErrorCode>(exception_ptr)) {
///     handle(*code);
///   }
///
/// See `rich_error_code` docblock for how to make your `Code` usable here.
///
/// NOTE: This only queries for the exact `Code` type supplied, and does not
/// look for any conversion paths from the codes supported by the underlying
/// error to the `Code`.  This is OK and the right default behavior (esp since
/// it's uncommon and frankly weird to design code hierarchies that are
/// implicitly interconvertible).  However, if a compelling use-case arose,
/// then you could introduce a separate "converting" query API, and export the
/// available conversions via the `rich_error_code<>` trait:
///   optional<uintptr_t> rich_error_code<Code>::convert_from_uuid(...);
template <typename Code>
class get_rich_error_code_fn {
 public:
  template <typename T>
  [[nodiscard]] constexpr std::optional<Code> operator()(const T& t) const {
    // Per `retrieve_code`, `mangled_code_` is uninitialized for reserved UUIDs!
    //
    // This was a `requires` clause, but some GCC versions have a bug where
    // constraint normalization differs between friend declarations and class
    // definitions, causing "redeclaration with different constraints" errors.
    static_assert(
        rich_error_code<Code>::uuid > rich_error_code_query::kMaxReservedUuid);
    rich_error_code_query query{rich_error_code<Code>::uuid};
    detail::get_rich_error_code_traits<T, void>::retrieve_code(t, query);
    if (query.has_value_) {
      return detail::unmangle_rich_error_code<Code>(query.mangled_code_);
    }
    return std::nullopt;
  }
};
template <typename Code>
inline constexpr get_rich_error_code_fn<Code> get_rich_error_code{};

} // namespace folly

#endif // FOLLY_HAS_RESULT
