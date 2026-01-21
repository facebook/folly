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
#include <folly/Traits.h>
#include <folly/lang/Pretty.h>
#include <folly/result/rich_error_base.h>

// Shared details for `rich_error.h` and `immortal_rich_error.h`.

#if FOLLY_HAS_RESULT

namespace folly::detail {

template <typename T>
struct rich_error_add_partial_message_impl : public T {
  using T::T;
  constexpr const char* partial_message() const noexcept override {
    return pretty_name<T>();
  }
};

// Non-abstract only if `T` implements `partial_message()`
template <typename T>
class rich_error_test_for_partial_message : T {
 private:
  rich_error_test_for_partial_message() = delete; // don't construct or inherit
  void only_rich_error_may_instantiate(
      rich_error_base::only_rich_error_may_instantiate_t) override {}
};

// Adds `partial_message() only if `T` doesn't provide it.
//
// Design rationale: There are two reasonable ideas for having `rich_error<>`
// and `immortal_rich_error<>` automatically add in a simple `partial_message`
// if it's not already supplied:
//
//   - The current one.  It has the downside of making the class hierarchy
//     deeper for errors that don't implement their own.  This in turn can slow
//     down `dynamic_cast` (when our RTTI-free optimizations don't apply, see
//     `rich_exception_ptr_bench.cpp`).  However, in practice, production
//     errors should likely define a better message anyhow, so this is fine.
//
//   - Have the leaf classes always define `partial_message()`, and internally
//     delegate via `if constexpr ()` to the base if it has one.  This one is
//     also ok, but it adds code size for ALL error types, unconditionally.
template <typename T>
using rich_error_with_partial_message = conditional_t<
    !std::is_abstract_v<rich_error_test_for_partial_message<T>>,
    T,
    rich_error_add_partial_message_impl<T>>;

// Validate a user-defined "rich error" class `Ex` while instantiating
// `rich_error<Ex>` or `immortal_rich_error<Ex>`.
//
// Thanks to the `rich_error_base` passkey, a user should not be able to
// instantiate such an `Ex` directly, so this provides pretty good guarantees
// that `Ex*` can only be obtained from `...rich_error<Ex>`, and thus has
// passed these validations.
//
// Why isn't this a concept contraining class templates? Two reasons:
//   - A constraint prevents referencing `rich_error<A>` from `A`.
//   - Forward-declarations get messier.
//
// Future: Most user types for `rich_error` should **NOT** inherit from
// `std::exception`, since the leaf wrappers will add it to the dynamic type.
// But, we cannot uniformly enforce this since there are valid uses that do
// need it on the base.  If the issue of "accidentally adding an
// `std::exception` diamond" is both common and problematic, we could either:
//   - Change `rich_error` to only add it if it's not already there.
//   - Ban it by default and add an API to bypass that check.
//
// `require_sizeof` since some checks are UB with incomplete types
template <
    typename UserEx,
    std::derived_from<UserEx> ActualEx, // rich_error<> or immortal...storage<>
    size_t = require_sizeof<UserEx>>
inline constexpr bool static_assert_is_valid_rich_error_type(const ActualEx*) {
  static_assert(
      std::derived_from<UserEx, rich_error_base>,
      "Rich error types must derive from `rich_error_base`.");
  // Without the offset-0 property, it would be UB to do the RTTI-free
  // `rich_error_base` access, as implemented by `rich_exception_ptr`.
  static_assert(
      detail::has_offset0_base<ActualEx, rich_error_base>,
      "When inheriting from `rich_error_base`, that type (or its derived type) "
      "must be first in the inheritance list.");
  // Did the user-provided type correctly use `rich_error_hints`?
  //
  // Future: If you have a strong case for NOT hinting a base error class in
  // its own definition, add a bypass for the "each type must hint at least
  // itself" rich error static assert.  The bypass can simply be a
  // `folly_`-prefixed member type alias that points to the current class, e.g.
  //   struct UnhintedBase : rich_error_base {
  //     using folly_rich_error_do_not_require_hint_for = UnhintedBase;
  //   };
  using Hint = typename UserEx::folly_get_exception_hint_types;
  static_assert(
      type_list_find_v<rich_error<UserEx>, Hint> < type_list_size_v<Hint>,
      "A rich error user type `T` should use `rich_error_hints<T>` to avoid "
      "RTTI costs. If this is a base class, read the 'hints' docblock.");
  return true;
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
