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

#include <tuple>
#include <type_traits>

#include <folly/Traits.h>
#include <folly/lang/bind/Bind.h>

///
/// Extends `Bind.h` (read that first) with keyword-argument syntax:
///   bind::args{
///      self_id = bind::in_place<MyClass>(), // tag akin to Python's `self`
///      "x"_id = 5,
///      "y"_id = mut_ref{y},
///   }
///
/// If the API takes a pack of named bindings, the above syntax is all that a
/// callers needs to know.  Some API mays require you to wrap the pack with a
/// custom container like `named_values{...}` (not yet built).
///

namespace folly::bind {

namespace ext { // For extension authors -- public details

template <auto Tag, std::derived_from<folly::bind::ext::bind_info_t> Base>
struct named_bind_info_t : Base {
  constexpr const Base& as_base() const { return *this; }
  using Base::Base;
  constexpr explicit named_bind_info_t(
      // Use a constraint to prevent object slicing.
      std::same_as<Base> auto b)
      : Base(std::move(b)) {}
};

namespace detail {
template <auto Tag>
struct named_bind_info {
  template <typename BI>
    requires(
        // Use a constraint to prevent object slicing.
        std::derived_from<BI, bind_info_t>
        // XXX Future: && a clause to prevent chaining assignments.
        )
  constexpr named_bind_info_t<Tag, BI> operator()(BI bi) const {
    return named_bind_info_t<Tag, BI>{std::move(bi)};
  }
};
} // namespace detail

template <literal_string Str, typename T>
struct id_arg : ext::merge_update_args<detail::named_bind_info<Str>, T> {
  using ext::merge_update_args<detail::named_bind_info<Str>, T>::
      merge_update_args;
};

struct self_id_t {};

template <typename T>
struct self_id_arg
    : ext::merge_update_args<detail::named_bind_info<self_id_t{}>, T> {
  using ext::merge_update_args<detail::named_bind_info<self_id_t{}>, T>::
      merge_update_args;
};

template <auto Tag> // `literal_string<"x">` or `self_id_t{}`
class identifier {
 private:
  template <typename T, literal_string Str>
  static constexpr id_arg<Str, T> make_with_tag(vtag_t<Str>, auto&& ba) {
    return id_arg<Str, T>{unsafe_move_args::from(std::move(ba))};
  }
  template <typename T>
  static constexpr self_id_arg<T> make_with_tag(
      vtag_t<self_id_t{}>, auto&& ba) {
    return self_id_arg<T>{unsafe_move_args::from(std::move(ba))};
  }

 public:
  // "x"_id = some_binding_modifier{var}
  constexpr auto operator=(std::derived_from<ext::like_args> auto ba) const {
    []<typename... BTs>(tag_t<BTs...>) {
      static_assert(
          sizeof...(BTs) == 1,
          "Cannot give the same name to multiple bound args.");
    }(typename decltype(ba)::binding_list_t{});
    return make_with_tag<decltype(ba)>(vtag<Tag>, std::move(ba));
  }
  // "x"_id = var
  template <typename T>
    requires(!std::derived_from<T, ext::like_args>)
  constexpr auto operator=(T&& t) const {
    return make_with_tag<T&&>(vtag<Tag>, args{static_cast<T&&>(t)});
  }

  static inline constexpr auto folly_bindings_identifier_tag = Tag;
};

} // namespace ext

// These types represents "identifier tag -> storage type" signatures for
// bindings. Library authors: search for `signature_type`.
//
// NB: `id_type` is distinct from `self_id_type` for cleaner error messages.
// Logically, both of these are `tag_type` (or similar), and the
// implementation could later be generalized, but for now, this is ok.
template <typename UnderlyingSig>
struct self_id_type {
  using identifier_type = ext::identifier<ext::self_id_t{}>;
  using storage_type = UnderlyingSig::storage_type;
};
template <literal_string Str, typename UnderlyingSig>
struct id_type {
  using identifier_type = ext::identifier<Str>;
  using storage_type = UnderlyingSig::storage_type;
};

template <literal_string Str>
inline constexpr ext::identifier<Str> id{};

// For brevity, `bind::id<"x">` can be made via the literal `"x"_id`.
inline namespace literals {
inline namespace string_literals {
template <literal_string Str>
consteval auto operator""_id() noexcept {
  return id<Str>;
}
} // namespace string_literals
} // namespace literals

// A special `identifier` tag, which works just like `"x"_id`. It's meant as
// syntax sugar for "self-reference" scenarios, such as:
//   - Tagging for the implicit scope/object parameter in `spawn_self_closure`.
//   - Tagging `this`-pointer-like objects when calling class member functions
//     with a kwarg calling convention.
// While it somewhat resembles the role of the `this` pointer, the name
// `this_id` is ambiguous (which ID?), and doesn't sound well in all use-cases.
inline constexpr ext::identifier<ext::self_id_t{}> self_id;

} // namespace folly::bind
