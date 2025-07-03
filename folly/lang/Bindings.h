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
#include <folly/detail/tuple.h>

FOLLY_PUSH_WARNING
FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS

///
/// READ ME: The docs for this library are in `Bindings.md`.
///

namespace folly::bindings {

template <typename, typename... As>
constexpr auto make_in_place(As&&...);
template <typename... As>
constexpr auto make_in_place_with(auto, As&&...);

// The primitive for representing lists of bound args. Unary forms:
//  - `bound_args<V>`: if `V` is already `like_bound_args`, wraps that
//    binding, unmodified.
//  - `bound_args<V&>` or `bound_args<V&&>`: binds a reference preserving
//    the value category of the input.
// Plural `bound_args<Ts...>` is a concatenation of many `like_bound_args`,
// equivalent to `merge_and_update_bound_args` with `std::identity`.
template <typename...>
class bound_args;

namespace detail::lite_tuple {
using namespace folly::detail::lite_tuple;
}

namespace ext { // For extension authors -- public details

// Any object modeling a list of bound args with modifiers must publicly
// derive from `like_bound_args`, and must also implement:
//  public:
//   using binding_list_t = ...;
//   constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]]
//   constexpr YOUR_CLASS(bound_args_unsafe_move, tuple_to_bind_t t);
class like_bound_args : NonCopyableNonMovable {};

struct bound_args_unsafe_move {
  template <std::derived_from<ext::like_bound_args> BoundArgs>
  static constexpr auto from(BoundArgs&& bargs) {
    return BoundArgs{
        bound_args_unsafe_move{},
        static_cast<BoundArgs&&>(bargs).unsafe_tuple_to_bind()};
  }
};

// A binding consists of:
//   - An "input type", most often a forwarding reference stored in the
//     `typename` of `binding_t`.  Or, if that is a value type, then we're
//     doing in-place construction, and the input is a "maker" object that's
//     implicitly convertible to that value type.
//   - "Flags" stored in `bind_info_t` enums below.  Users set them via
//     modifier helpers like `constant{}` or `const_ref{}`.  Library authors
//     may derive from `bind_info_t` to add custom flags, and then define a
//     corresponding `binding_policy` specialization to handle them.
//   - An "output type" computed **after** all the modifiers are applied
//     via a `binding_policy`, review the docs for the standard one below.
//
// Each flag must default to `unset` so that the policy can dictate
// behavior.  For example, standard policy is: non-`const` output for
// pass-by-value, `const` for pass-by-ref.  But, with this flag + policy
// design, an API can elect an alternate "always `const`" policy while still
// using the standard modifier vocabulary from this file.
enum class category_t {
  unset = 0, // The binding policy decides. The standard policy uses "VALUE".
  ref, // Follows reference category of the input, i.e. `InputT&&`.
  value, // Copies or moves, depending on input ref type.
  copy, // Like `value`, but fails on rvalue input.
  move // Like `value`, but fails on lvalue input.
};
enum class constness_t {
  // The binding policy decides.  The standard policy uses `const` for refs,
  // `non_constant` for values.
  unset = 0,
  // For reference types, these both affect the underlying value type:
  constant, // Make the input `const` if it's not already.
  non_constant // Will NOT remove `const` from an input type
};
struct bind_info_t {
  category_t category;
  constness_t constness;

  explicit bind_info_t() = default;
  constexpr explicit bind_info_t(category_t ct, constness_t cn)
      : category(ct), constness(cn) {}
};

// Metadata for a bound arg, capturing:
//   - `BindingType`: the input binding type -- a reference, unless this is
//     a `make_in_place*` binding -- see `is_binding_t_type_in_place`.
//   - A `bind_info_t`-derived object capturing the effects of any binding
//     modifiers (`constant`, `const_ref`, etc).
// This is used by `binding_policy` to compute `storage_type` and
// `signature_type`.  The values returned by `unsafe_tuple_to_bind()` should
// be convertible to the resulting `storage_type`.
template <std::derived_from<bind_info_t> auto, typename BindingType>
struct binding_t {};

// CAUTION: Applicable ONLY to `BT` from `binding_t<BI, BT>`.
// Contrast to `detail::is_in_place_maker`.
template <typename BT>
concept is_binding_t_type_in_place = !std::is_reference_v<BT>;

// Implementation detail for all the `bound_args` modifiers (`constant`,
// `const_ref`, etc).  Concatenates multiple `Ts`, each quacking like
// `like_bound_args`, to produce a single `like_bound_args` list.  Applies
// `BindInfoFn` to each `bind_info_t` in the inputs' `binding_list_t`s.
template <typename BindInfoFn, typename... Ts>
class merge_update_bound_args : public like_bound_args {
 private:
  using tuple_to_bind_t = decltype(detail::lite_tuple::tuple_cat(
      std::declval<bound_args<Ts>>().unsafe_tuple_to_bind()...));
  tuple_to_bind_t tup_;

 protected:
  ~merge_update_bound_args() = default; // Only used as a base class.

 public:
  // Concatenate `Ts::binding_list_t...` after mapping their `bind_info_t`s
  // through `BindInfoFn`.
  using binding_list_t = decltype([]<auto... BIs, typename... BTs>(
                                      tag_t<binding_t<BIs, BTs>...>) {
    return tag<binding_t<BindInfoFn{}(BIs), BTs>...>;
  }(type_list_concat_t<tag_t, typename bound_args<Ts>::binding_list_t...>{}));

  explicit constexpr merge_update_bound_args(bound_args<Ts>... ts) noexcept
      : tup_(detail::lite_tuple::tuple_cat(
            std::move(ts).unsafe_tuple_to_bind()...)) {}

  // `lifetimebound` documented in `in_place_bound_args_crtp_base`
  constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]] {
    return std::move(tup_);
  }

  constexpr merge_update_bound_args(bound_args_unsafe_move, tuple_to_bind_t t)
      : tup_(std::move(t)) {}
};

// This is cosmetic -- the point is for the signatures of `bound_args<>` and
// similar templates to show rvalue reference bindings with `&&`.
template <typename T>
using deduce_bound_args_t =
    std::conditional_t<std::derived_from<T, like_bound_args>, T, T&&>;

} // namespace ext

// Binds an input reference (lvalue or rvalue)
template <typename T>
  requires(!std::derived_from<T, ext::like_bound_args>)
class bound_args<T> : public ext::like_bound_args {
  static_assert(
      std::is_reference_v<T>,
      "Check that your deduction guide has `deduce_bound_args_t`");

 private:
  T& ref_;

 public:
  using binding_list_t = tag_t<ext::binding_t<ext::bind_info_t{}, T&&>>;

  constexpr /*implicit*/ bound_args(T&& t) noexcept : ref_(t) {}

  // `lifetimebound` documented in `in_place_bound_args_crtp_base`
  constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]] {
    return detail::lite_tuple::tuple<T&&>{static_cast<T&&>(ref_)};
  }

  constexpr bound_args(
      ext::bound_args_unsafe_move, detail::lite_tuple::tuple<T&&> t)
      : ref_(detail::lite_tuple::get<0>(t)) {}
};

// This specialization is instantiated when a binding modifier (usually
// derived from `merge_update_bound_args`), or a `make_in_place*` binding,
// gets passed to an object taking `bound_args<Ts>...`.
//
// It wraps another `like_bound_args`, by value.  This preserves the
// interface (`binding_list_t`, `unsafe_tuple_to_bind`, etc) of the
// underlying class.  It only exists to allow constructors of bound args
// aggregates to take their inputs by-value as `bound_args<Ts>...`.
//
// This "always wrap" design is necessary because `like_bound_args` are both
// immovable AND unsafe for end-users to pass by-reference.  They contain
// references themselves, so they should only exist as prvalues for the
// duration of one statement (so that C++ reference lifetime extension
// guarantees safety).  So, we pass them by-value, and use the
// `bound_args_unsafe_move` ctor to move the innards from the prvalue argument
// into the wrapper.
//
// NB It is not typical for `T` to be another `bound_args`, but it's also
// perfectly fine, compositionally speaking, so it is allowed.
template <std::derived_from<ext::like_bound_args> T>
class bound_args<T> : public T {
 public:
  constexpr /*implicit*/ bound_args(T t)
      : T(ext::bound_args_unsafe_move{}, std::move(t).unsafe_tuple_to_bind()) {}

  constexpr bound_args(ext::bound_args_unsafe_move, auto tup)
      : T(ext::bound_args_unsafe_move{}, std::move(tup)) {}
};

template <typename... Ts>
  requires(sizeof...(Ts) != 1)
class bound_args<Ts...>
    : public ext::merge_update_bound_args<std::identity, Ts...> {
  using ext::merge_update_bound_args<std::identity, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
bound_args(Ts&&...) -> bound_args<ext::deduce_bound_args_t<Ts>...>;

namespace detail { // Private details

template <typename T, typename Maker>
class in_place_bound_args_crtp_base
    : public ::folly::bindings::ext::like_bound_args {
 private:
  static_assert(!std::is_reference_v<T>);
  static_assert( // This would be an unexpected usage.
      !std::derived_from<T, ::folly::bindings::ext::like_bound_args>);

 protected:
  using maker_type = Maker;
  maker_type maker_;

  constexpr in_place_bound_args_crtp_base(
      ext::bound_args_unsafe_move, detail::lite_tuple::tuple<maker_type> t)
      : maker_(std::move(detail::lite_tuple::get<0>(t))) {}

  constexpr explicit in_place_bound_args_crtp_base(maker_type maker)
      : maker_(std::move(maker)) {}

 public:
  using binding_list_t = tag_t<ext::binding_t<ext::bind_info_t{}, T>>;

  // Technically, the `lifetimebound` below is too conservative, because we
  // hand ownership of the refs in `maker_` to the caller.  However, since
  // `like_bound_args` must never exist for more than one statement, this
  // should not be a problem in practical usage.
  //
  // The annotation's benefit is that it detects real implementation bugs.
  // See `all_tests_run_at_build_time` for a manually compilable example.
  // In short, if you removed this `lifetimebound`, the compiler could no
  // longer catch this dangling ref --
  //   // BAD: Contained prvalue `&made` becomes invalid at the `;`
  //   auto fooMaker = make_in_place<Foo>(&made, n).unsafe_tuple_to_bind();
  //   Foo foo = std::move(fooMaker);

  // To allow in-place construction within a `tuple<..., T, ...>`, this
  // returns a `Maker` that's implicitly convertible to `T`.
  constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]] {
    return detail::lite_tuple::tuple{std::move(maker_)};
  }
};

// Both "maker" classes are move-only to help keep them single-use.
template <typename T, typename... Args>
class in_place_args_maker : private MoveOnly {
 private:
  // `&&` allows binding rvalues. Safe, since a binding lives for 1 statement.
  detail::lite_tuple::tuple<Args&&...> arg_tup_;

 protected:
  template <typename, typename...>
  friend class in_place_bound_args;

  constexpr /*implicit*/ in_place_args_maker(
      Args&&... as [[clang::lifetimebound]]) noexcept
      : arg_tup_{static_cast<Args&&>(as)...} {}

 public:
  // This implicit conversion allows direct construction inside of `tuple` e.g.
  constexpr /*implicit*/ operator T() && {
    return lite_tuple::apply(
        [](auto&&... as) { return T{static_cast<Args&&>(as)...}; },
        std::move(arg_tup_));
  }
  // Power users may want to rewrite the args of an in-place binding.
  constexpr auto&& release_arg_tuple() && noexcept [[clang::lifetimebound]] {
    return std::move(arg_tup_);
  }
};

// NB: `Args` are deduced by `make_in_place` as forwarding references
template <typename T, typename... Args>
class in_place_bound_args
    : public in_place_bound_args_crtp_base<T, in_place_args_maker<T, Args...>> {
 protected:
  template <typename, typename... As>
  friend constexpr auto ::folly::bindings::make_in_place(As&&...);

  using base =
      in_place_bound_args_crtp_base<T, in_place_args_maker<T, Args...>>;
  constexpr explicit in_place_bound_args(
      Args&&... args [[clang::lifetimebound]])
      : base{{static_cast<Args&&>(args)...}} {}

 public:
  constexpr in_place_bound_args(ext::bound_args_unsafe_move u, auto t)
      : base{u, std::move(t)} {}
};

template <typename T, typename Fn, typename... Args>
class in_place_fn_maker : private MoveOnly {
 private:
  Fn fn_; // May contain refs; ~safe since a binding lives for 1 statement.
  detail::lite_tuple::tuple<Args&&...> arg_tup_;

 protected:
  template <typename, typename, typename...>
  friend class in_place_fn_bound_args;

  constexpr /*implicit*/ in_place_fn_maker(
      Fn fn, Args&&... as [[clang::lifetimebound]])
      : fn_(std::move(fn)), arg_tup_{static_cast<Args&&>(as)...} {}

 public:
  // This implicit conversion allows direct construction inside of `tuple` e.g.
  constexpr /*implicit*/ operator T() && {
    return lite_tuple::apply(fn_, arg_tup_);
  }
};

// NB: `Args` are deduced by `make_in_place` as forwarding references
template <typename T, typename Fn, typename... Args>
class in_place_fn_bound_args
    : public in_place_bound_args_crtp_base<
          T,
          in_place_fn_maker<T, Fn, Args...>> {
 protected:
  template <typename... As>
  friend constexpr auto ::folly::bindings::make_in_place_with(auto, As&&...);

  using base =
      in_place_bound_args_crtp_base<T, in_place_fn_maker<T, Fn, Args...>>;
  constexpr explicit in_place_fn_bound_args(
      Fn fn, Args&&... args [[clang::lifetimebound]])
      : base{{std::move(fn), static_cast<Args&&>(args)...}} {}

 public:
  constexpr in_place_fn_bound_args(ext::bound_args_unsafe_move u, auto t)
      : base{u, std::move(t)} {}
};

// If using this with `BT` from `binding_t<BI, BT>`, do not `remove_reference`
// first, since such `BT` is always a value for in-place bindings.
template <typename T>
concept is_in_place_maker = instantiated_from<T, in_place_fn_maker> ||
    instantiated_from<T, in_place_args_maker>;

using constant_bind_info = decltype([](auto bi) {
  bi.constness = ext::constness_t::constant;
  return bi;
});

using non_constant_bind_info = decltype([](auto bi) {
  bi.constness = ext::constness_t::non_constant;
  return bi;
});

using const_ref_bind_info = decltype([](auto bi) {
  bi.category = ext::category_t::ref;
  bi.constness = ext::constness_t::constant;
  return bi;
});

using mut_ref_bind_info = decltype([](auto bi) {
  bi.category = ext::category_t::ref;
  bi.constness = ext::constness_t::non_constant;
  return bi;
});

} // namespace detail

// `make_in_place` and `make_in_place_with` construct non-movable,
// non-copyable types in their final location.
//
// CAREFUL: As with other `bound_args{}`, the returned object stores references
// to `args` to avoid unnecessary move-copies.  A power user may wish to write
// a function that returns a binding, which OWNS some values.  For example, you
// may need to avoid a stack-use-after-free such as this one:
//
//   auto makeFoo(int n) { return make_in_place<Foo>(n); } // BAD: `n` is dead
//
// To avoid the bug, either take `n` by-reference (often preferred), or store
// your values inside a `make_in_place_with` callable:
//
//   auto makeFoo(int n) {
//     return make_in_place_with([n]() { return Foo{n}; });
//   }
template <typename T, typename... Args>
constexpr auto make_in_place(Args&&... args [[clang::lifetimebound]]) {
  return detail::in_place_bound_args<T, Args...>{static_cast<Args&&>(args)...};
}
// This is second-choice compared to `make_in_place` because:
//   - Dangling references may be hidden inside `make_fn` captures --
//     `clang` offers no `lifetimebound` analysis for these (yet?).
//   - The type signature of the `in_place_bound_args` includes a lambda.
// CAREFUL: While `make_fn` is taken by-value, `args` are stored as references,
// as in `make_in_place`.
template <typename... Args>
constexpr auto make_in_place_with(
    auto make_fn, Args&&... args [[clang::lifetimebound]]) {
  return detail::in_place_fn_bound_args<
      std::invoke_result_t<decltype(make_fn), Args&&...>,
      decltype(make_fn),
      Args...>{std::move(make_fn), static_cast<Args&&>(args)...};
}

// The below "binding modifiers" all return an immovable bound args list,
// meant to be passed only by-value, as a prvalue.  They can all take unary,
// or plural `like_bound_args` as inputs.  Their only difference from
// `bound_args{values...}` is that these modifiers override some aspect of
// `bind_info_t`s on on all the bindings they contain.
//
// Unlike standard C++, the "constant" and "ref" modifiers commute, avoiding
// that common footgun.
//
// You can think of these analogously to value category specifiers for
// member variables of a struct -- e.g.
//
//   (1) `mut_ref{}` asks to store a non-const reference, of
//        the same reference category as the input.  Or, see `const_ref{}`.
//   (2) `constant{}` stores the input as a `const` value (under the default
//       `binding_policy`).  Unlike `std::as_const` this allows you to move
//        a non-const reference into a `const` storage location.
//
// Specifiers can be overridden, e.g. you could (but should not!) express
// `mut_ref{}` as `non_constant{const_ref{}}`.
//
// There's currently no user-facing `by_ref{}`, which would leave the
// `constness` of the binding to be defaulted by the `binding_policy` below.
// If a use-case arises, the test already contains its trivial implementation.

template <typename... Ts>
struct constant
    : ext::merge_update_bound_args<detail::constant_bind_info, Ts...> {
  using ext::merge_update_bound_args<detail::constant_bind_info, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
constant(Ts&&...) -> constant<ext::deduce_bound_args_t<Ts>...>;

template <typename... Ts>
struct non_constant
    : ext::merge_update_bound_args<detail::non_constant_bind_info, Ts...> {
  using ext::merge_update_bound_args<detail::non_constant_bind_info, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
non_constant(Ts&&...) -> non_constant<ext::deduce_bound_args_t<Ts>...>;

template <typename... Ts>
struct const_ref
    : ext::merge_update_bound_args<detail::const_ref_bind_info, Ts...> {
  using ext::merge_update_bound_args<detail::const_ref_bind_info, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
const_ref(Ts&&...) -> const_ref<ext::deduce_bound_args_t<Ts>...>;

template <typename... Ts>
struct mut_ref
    : ext::merge_update_bound_args<detail::mut_ref_bind_info, Ts...> {
  using ext::merge_update_bound_args<detail::mut_ref_bind_info, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
mut_ref(Ts&&...) -> mut_ref<ext::deduce_bound_args_t<Ts>...>;

// Future: Add `copied()` and `moved()` modifiers so the user can ensure
// pass-by-value with copy-, or move-copy semantics.  This enforcement
// already exists in `binding_policy` and `category_t`.

namespace ext { // For extension authors -- public details

template <typename, typename = void>
class binding_policy;

// This is a separate class so that libraries can customize the binding
// policy enacted by `binding_policy` by detecting their custom fields in
// `BI`.  See `named/Binding.h` for an example.
//
// IMPORTANT:
//  - Only specialize `binding_policy` for the specific derived classes
//    of `bind_info_t` that you own -- DO NOT overmatch!
//  - DO delegate handling of the base `bind_info_t` to this specialization, by
//    slicing your input BI. Example:
//      using basics = binding_policy<binding_t<bind_info_t{BI}, BindingType>>;
//    Any deviations from the standard policy are likely to confuse users &
//    readers of your library, and are probably not worth it.  If you REALLY
//    need to deviate (ex: default bindings to `const`), make the name of
//    your API reflect this (ex: `fooDefaultConst()`).
template <auto BI, typename BindingType>
// Formulated as a constraint to prevent object slicing
  requires std::same_as<decltype(BI), bind_info_t>
class binding_policy<binding_t<BI, BindingType>> {
  static_assert(
      !is_binding_t_type_in_place<BindingType> ||
          BI.category != category_t::ref,
      "`const_ref` / `mut_ref` is incompatible with `make_in_place*`");

 protected:
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
      if constexpr (BI.constness == constness_t::non_constant) {
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

} // namespace ext
} // namespace folly::bindings

FOLLY_POP_WARNING
