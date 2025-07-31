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

#include <folly/Utility.h>
#include <folly/lang/bind/Bind.h>

///
/// Implements the bindings semantics of `safe_closure()`.  In this example, it
/// is responsible for for the "pass by" portion of the logic -- by the time we
/// invoke `bind_as_argument`, the input ref was already stored as a value
/// inside the `safe_closure`.
///
///   bind::args{
///       // move `a` in; pass it by const ref
///       std::move(a),
///       // copy `b` in; pass it by mutable ref
///       // deletes `operator() const &`
///       bind::non_constant{b},
///       // construct the prvalue inside `fn`
///       // when passing, decay-copy our stored argument
///       bind::copy{c()},
///       // move `d` in; pass it by rvalue ref
///       // deletes all overloads but `operator() &&`
///       bind::move{std::move(d)}}
///

namespace folly::bind::ext { // Details for library authors, not API consumers

template <auto BI, typename StorageRef>
// Written as a constraint to prevent object slicing
  requires std::same_as<decltype(BI), bind_info_t>
constexpr decltype(auto) bind_as_argument(StorageRef&& sr) {
  if constexpr ( // Default to pass-by-const-ref
      BI.category == category_t::ref || BI.category == category_t::unset) {
    if constexpr (
        BI.constness == constness_t::constant ||
        BI.constness == constness_t::unset) {
      // This is like `std::as_const`, but with support for rvalues.
      // We want rvalue support so that code like this can compile:
      //   safe_closure(bind::args{a}, [](auto&){})();
      return static_cast<detail::add_const_inside_ref_t<StorageRef&&>>(sr);
    } else {
      static_assert(BI.constness == constness_t::mut);
      // In principle, this could be relaxed, but we'd need to see a use-case
      // where binding `bind::mut` by `const` reference is justified.
      static_assert(
          !std::is_const_v<std::remove_reference_t<StorageRef>>,
          "It is likely a bug to use `bind::mut` when the stored argument "
          "(e.g. the `safe_closure`) is `const`. Hint: If you're invoking a "
          "lambda capture, recall that copy-captures are `const` by default.");
      return static_cast<StorageRef&&>(sr);
    }
  } else { // Explicit pass-by-value: `bind::move{}` or `bind::copy{}`
    // There's no explicit modifier for this yet, nor is it clear why you'd
    // want one in an argument-passing setting.
    static_assert(BI.category != category_t::value);
    // Can relax this assert if a use-case dictates sensible semantics.
    static_assert(
        BI.constness == constness_t::unset,
        "It's unclear what `bind::const` / `bind::mut` means when passing "
        "arguments by-value with `bind::move` or `bind::copy`.");
    if constexpr (BI.category == category_t::copy) {
      // Safe to relax, but it's likely a sign of a confused user.
      static_assert(
          std::is_reference_v<StorageRef>,
          "Copying from rvalue storage. Did you mean `bind::move`?");
      return ::folly::copy(std::forward<StorageRef>(sr));
    } else {
      // Fail on `category_t::value`.  It has no explicit modifier yet, nor is
      // it clear why you'd want one in an argument-passing setting.
      static_assert(BI.category == category_t::move);
      // Required so that use-after-move linters consider the storage to be in
      // a moved-out state.  I.e. "move" is destructive.
      static_assert(
          !std::is_reference_v<StorageRef>,
          "To use `bind::move`, pass the arg storage (e.g. the `safe_closure`) "
          "by rvalue. Hint: if your `safe_closure` is a lambda capture, recall "
          "that copy-captures are `const` by default.");
      return static_cast<StorageRef&&>(sr);
    }
  }
}

} // namespace folly::bind::ext
