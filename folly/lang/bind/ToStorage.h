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
/// Implements standard storage semantics for `folly::bind`.  This is used
/// (with some additional constraints & tweaks) by `async_closure` captures.
/// It can also be used for other single-use data structurs -- this makes it
/// easy to simultaneously define the types for, and construct, tuples, named
/// tuples, structs, etc. Some examples of this are given in `Bind.md`.
///

namespace folly::bind::ext { // Details for library authors, not API consumers

template <typename, typename = void>
class bind_to_storage_policy;

// This is a separate class so that libraries can customize the binding
// policy enacted by `bind_to_storage_policy` by detecting their custom fields
// in `BI`.  See `named/Binding.h` for an example.
//
// IMPORTANT:
//  - Only specialize `bind_to_storage_policy` for the specific derived classes
//    of `bind_info_t` that you own -- DO NOT overmatch!
//
//  - DO delegate handling of the base `bind_info_t` to this specialization, by
//    slicing your input BI.  See `NamedToStorage.h` for an example & a
//    relevant future refactor.
//
//    Any deviations from the standard policy are likely to confuse users &
//    readers of your library, and are probably not worth it.  If you REALLY
//    need to deviate (ex: default bindings to `const`), make the name of
//    your API reflect this (ex: `fooDefaultConst()`).
template <auto BI, typename BindingType>
// Formulated as a constraint to prevent object slicing
  requires std::same_as<decltype(BI), bind_info_t>
class bind_to_storage_policy<binding_t<BI, BindingType>> {
  static_assert(
      !is_binding_t_type_in_place<BindingType> ||
          BI.category != category_t::ref,
      "`const_ref` / `mut_ref` is incompatible with `bind::in_place*`");

 protected:
  // Similar to `std::as_const`, but only computes the type, and works on
  // rvalue references.
  //
  // Future: This **might** compile faster with a family of explicit
  // specializations, see e.g. `folly::like_t`'s implementation.
  template <typename T>
  using add_const_inside_ref = std::conditional_t<
      std::is_rvalue_reference_v<T>,
      typename std::add_const<std::remove_reference_t<T>>::type&&,
      std::conditional_t<
          std::is_lvalue_reference_v<T>,
          typename std::add_const<std::remove_reference_t<T>>::type&,
          typename std::add_const<std::remove_reference_t<T>>::type>>;

  constexpr static auto project_type() {
    if constexpr (BI.category == category_t::ref) {
      // By-reference: `const` by default
      if constexpr (BI.constness == constness_t::mut) {
        return std::type_identity<BindingType&&>{}; // Leave existing `const`
      } else {
        return std::type_identity<add_const_inside_ref<BindingType>&&>{};
      }
    } else {
      if constexpr (BI.category == category_t::copy) {
        static_assert(!std::is_rvalue_reference_v<BindingType>);
      } else if constexpr (BI.category == category_t::move) {
        static_assert(std::is_rvalue_reference_v<BindingType>);
      } else {
        static_assert(
            (BI.category == category_t::value) ||
            (BI.category == category_t::unset));
      }
      // By-value: non-`const` by default
      using UnrefT = std::remove_reference_t<BindingType>;
      if constexpr (BI.constness == constness_t::constant) {
        return std::type_identity<const UnrefT>{};
      } else {
        return std::type_identity<UnrefT>{};
      }
    }
  }

 public:
  using storage_type = typename decltype(project_type())::type;
  // Why is this here?  With named bindings, we want the signature of a
  // binding list to show the identifier's name to the user.
  using signature_type = storage_type;
};

} // namespace folly::bind::ext
