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
/// READ ME: The docs for this library are in `Bind.md`.
///

namespace folly::bind {

template <typename, typename... As>
constexpr auto in_place(As&&...);
template <typename... As>
constexpr auto in_place_with(auto, As&&...);

// The primitive for representing lists of bound args. Unary forms:
//  - `bind::args<V>`: if `V` is already `like_args`, wraps that
//    binding, unmodified.
//  - `bind::args<V&>` or `bind::args<V&&>`: binds a reference preserving
//    the value category of the input.
// Plural `bind::args<Ts...>` is a concatenation of many `like_args`,
// equivalent to `merge_update_args` with `std::identity`.
template <typename...>
class args;

namespace detail::lite_tuple {
using namespace folly::detail::lite_tuple;
}

namespace ext { // For extension authors -- public details

// Any object modeling a list of bound args with modifiers must publicly
// derive from `like_args`, and must also implement:
//  public:
//   using binding_list_t = ...;
//   constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]]
//   constexpr YOUR_CLASS(unsafe_move_args, tuple_to_bind_t t);
class like_args : NonCopyableNonMovable {};

struct unsafe_move_args {
  template <std::derived_from<ext::like_args> BoundArgs>
  static constexpr auto from(BoundArgs&& bargs) {
    return BoundArgs{
        unsafe_move_args{},
        static_cast<BoundArgs&&>(bargs).unsafe_tuple_to_bind()};
  }
};

// A binding consists of:
//   - An "input type", most often a forwarding reference stored in the
//     `typename` of `binding_t`.  Or, if that is a value type, then we're
//     doing in-place construction, and the input is a "maker" object that's
//     implicitly convertible to that value type.
//   - "Flags" stored in `bind_info_t` enums below.  Users set them via
//     modifiers like `constant{}` or `const_ref{}`.  Library authors may
//     derive from `bind_info_t` to add custom flags, and then customize the
//     policies in `ToStorage.h` or `AsArgument.h` to handle them.
//
// Each flag must default to `unset` so that each policy can dictate behavior.
// For example, standard storage policy is: non-`const` output for
// pass-by-value, `const` for pass-by-ref.  But, with this flag + policy
// design, an API can elect an alternate "always `const`" policy while still
// using the standard modifier vocabulary from this file.
enum class category_t {
  // The policy decides.  The standard storage policy uses `value`, while the
  // argument-binding one uses `ref`.
  unset = 0,
  ref, // Follows reference category of the input, i.e. `InputT&&`.
  value, // Copies or moves, depending on input ref type.
  copy, // Like `value`, but fails on rvalue input.
  move // Like `value`, but fails on lvalue input.
};
enum class constness_t {
  // The policy decides.  The standard storage & argument-binding policies use
  // `const` for refs, `mut` for values.
  unset = 0,
  // For reference types, these both affect the underlying value type:
  constant, // Make the input `const` if it's not already.
  mut // Will NOT remove `const` from an input type
};
struct bind_info_t {
  category_t category;
  constness_t constness;

  explicit bind_info_t() = default;
  constexpr explicit bind_info_t(category_t ct, constness_t cn)
      : category(ct), constness(cn) {}
};

// Metadata for a bound arg, capturing:
//   - `BindingType`: the input binding type -- a reference, unless this is a
//     `bind::in_place*` -- see `is_binding_t_type_in_place`.
//   - A `bind_info_t`-derived object capturing the effects of any binding
//     modifiers (`constant`, `const_ref`, etc).
//
// This is used by policies like `bind_to_storage_policy` and
// `bind_as_argument()`.  The values returned by `unsafe_tuple_to_bind()`
// should be convertible to the resulting `::storage_type`.
template <std::derived_from<bind_info_t> auto, typename BindingType>
struct binding_t {};

// CAUTION: Applicable ONLY to `BT` from `binding_t<BI, BT>`.
// Contrast to `detail::is_in_place_maker`.
template <typename BT>
concept is_binding_t_type_in_place = !std::is_reference_v<BT>;

// Implementation detail for all the `bind::args` modifiers (`constant`,
// `const_ref`, etc).  Concatenates multiple `Ts`, each quacking like
// `like_args`, to produce a single `like_args` list.  Applies
// `BindInfoFn` to each `bind_info_t` in the inputs' `binding_list_t`s.
template <typename BindInfoFn, typename... Ts>
class merge_update_args : public like_args {
 private:
  using tuple_to_bind_t = decltype(detail::lite_tuple::tuple_cat(
      std::declval<args<Ts>>().unsafe_tuple_to_bind()...));
  tuple_to_bind_t tup_;

 protected:
  ~merge_update_args() = default; // Only used as a base class.

 public:
  // Concatenate `Ts::binding_list_t...` after mapping their `bind_info_t`s
  // through `BindInfoFn`.
  using binding_list_t =
      decltype([]<auto... BIs, typename... BTs>(tag_t<binding_t<BIs, BTs>...>) {
        return tag<binding_t<BindInfoFn{}(BIs), BTs>...>;
      }(type_list_concat_t<tag_t, typename args<Ts>::binding_list_t...>{}));

  explicit constexpr merge_update_args(args<Ts>... ts) noexcept
      : tup_(detail::lite_tuple::tuple_cat(
            std::move(ts).unsafe_tuple_to_bind()...)) {}

  // `lifetimebound` documented in `in_place_args_crtp_base`
  constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]] {
    return std::move(tup_);
  }

  constexpr merge_update_args(unsafe_move_args, tuple_to_bind_t t)
      : tup_(std::move(t)) {}
};

// This is cosmetic -- the point is for the signatures of `bind::args<>` and
// similar templates to show rvalue reference bindings with `&&`.
template <typename T>
using deduce_args_t =
    std::conditional_t<std::derived_from<T, like_args>, T, T&&>;

} // namespace ext

// Binds an input reference (lvalue or rvalue)
template <typename T>
  requires(!std::derived_from<T, ext::like_args>)
class args<T> : public ext::like_args {
  static_assert(
      std::is_reference_v<T>,
      "Check that your deduction guide has `deduce_args_t`");

 private:
  T& ref_;

 public:
  using binding_list_t = tag_t<ext::binding_t<ext::bind_info_t{}, T&&>>;

  constexpr /*implicit*/ args(T&& t) noexcept : ref_(t) {}

  // `lifetimebound` documented in `in_place_args_crtp_base`
  constexpr auto unsafe_tuple_to_bind() && noexcept [[clang::lifetimebound]] {
    return detail::lite_tuple::tuple<T&&>{static_cast<T&&>(ref_)};
  }

  constexpr args(ext::unsafe_move_args, detail::lite_tuple::tuple<T&&> t)
      : ref_(detail::lite_tuple::get<0>(t)) {}
};

// This specialization is instantiated when a binding modifier (usually derived
// from `merge_update_args`), or a `bind::in_place*`, gets passed to an
// object taking `bind::args<Ts>...`.
//
// It wraps another `like_args`, by value.  This preserves the
// interface (`binding_list_t`, `unsafe_tuple_to_bind`, etc) of the
// underlying class.  It only exists to allow constructors of bound args
// aggregates to take their inputs by-value as `bind::args<Ts>...`.
//
// This "always wrap" design is necessary because `like_args` are both
// immovable AND unsafe for end-users to pass by-reference.  They contain
// references themselves, so they should only exist as prvalues for the
// duration of one statement (so that C++ reference lifetime extension
// guarantees safety).  So, we pass them by-value, and use the
// `unsafe_move_args` ctor to move the innards from the prvalue argument
// into the wrapper.
//
// NB It is not typical for `T` to be another `bind::args`, but it's also
// perfectly fine, compositionally speaking, so it is allowed.
template <std::derived_from<ext::like_args> T>
class args<T> : public T {
 public:
  constexpr /*implicit*/ args(T t)
      : T(ext::unsafe_move_args{}, std::move(t).unsafe_tuple_to_bind()) {}

  constexpr args(ext::unsafe_move_args, auto tup)
      : T(ext::unsafe_move_args{}, std::move(tup)) {}
};

template <typename... Ts>
  requires(sizeof...(Ts) != 1)
class args<Ts...> : public ext::merge_update_args<std::identity, Ts...> {
  using ext::merge_update_args<std::identity, Ts...>::merge_update_args;
};
template <typename... Ts>
args(Ts&&...) -> args<ext::deduce_args_t<Ts>...>;

namespace detail { // Private details

template <typename T, typename Maker>
class in_place_args_crtp_base : public ::folly::bind::ext::like_args {
 private:
  static_assert(!std::is_reference_v<T>);
  static_assert( // This would be an unexpected usage.
      !std::derived_from<T, ::folly::bind::ext::like_args>);

 protected:
  using maker_type = Maker;
  maker_type maker_;

  constexpr in_place_args_crtp_base(
      ext::unsafe_move_args, detail::lite_tuple::tuple<maker_type> t)
      : maker_(std::move(detail::lite_tuple::get<0>(t))) {}

  constexpr explicit in_place_args_crtp_base(maker_type maker)
      : maker_(std::move(maker)) {}

 public:
  using binding_list_t = tag_t<ext::binding_t<ext::bind_info_t{}, T>>;

  // Technically, the `lifetimebound` below is too conservative, because we
  // hand ownership of the refs in `maker_` to the caller.  However, since
  // `like_args` must never exist for more than one statement, this
  // should not be a problem in practical usage.
  //
  // The annotation's benefit is that it detects real implementation bugs.
  // See `all_tests_run_at_build_time` for a manually compilable example.
  // In short, if you removed this `lifetimebound`, the compiler could no
  // longer catch this dangling ref --
  //   // BAD: Contained prvalue `&made` becomes invalid at the `;`
  //   auto fooMaker = bind::in_place<Foo>(&made, n).unsafe_tuple_to_bind();
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
  friend class in_place_args;

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

// NB: `Args` are deduced by `bind::in_place` as forwarding references
template <typename T, typename... Args>
class in_place_args
    : public in_place_args_crtp_base<T, in_place_args_maker<T, Args...>> {
 protected:
  template <typename, typename... As>
  friend constexpr auto ::folly::bind::in_place(As&&...);

  using base = in_place_args_crtp_base<T, in_place_args_maker<T, Args...>>;
  constexpr explicit in_place_args(Args&&... args [[clang::lifetimebound]])
      : base{{static_cast<Args&&>(args)...}} {}

 public:
  constexpr in_place_args(ext::unsafe_move_args u, auto t)
      : base{u, std::move(t)} {}
};

template <typename T, typename Fn, typename... Args>
class in_place_fn_maker : private MoveOnly {
 private:
  Fn fn_; // May contain refs; ~safe since a binding lives for 1 statement.
  detail::lite_tuple::tuple<Args&&...> arg_tup_;

 protected:
  template <typename, typename, typename...>
  friend class in_place_fn_args;

  constexpr /*implicit*/ in_place_fn_maker(
      Fn fn, Args&&... as [[clang::lifetimebound]])
      : fn_(std::move(fn)), arg_tup_{static_cast<Args&&>(as)...} {}

 public:
  // This implicit conversion allows direct construction inside of `tuple` e.g.
  constexpr /*implicit*/ operator T() && {
    return lite_tuple::apply(fn_, arg_tup_);
  }
};

// NB: `Args` are deduced by `bind::in_place` as forwarding references
template <typename T, typename Fn, typename... Args>
class in_place_fn_args
    : public in_place_args_crtp_base<T, in_place_fn_maker<T, Fn, Args...>> {
 protected:
  template <typename... As>
  friend constexpr auto ::folly::bind::in_place_with(auto, As&&...);

  using base = in_place_args_crtp_base<T, in_place_fn_maker<T, Fn, Args...>>;
  constexpr explicit in_place_fn_args(
      Fn fn, Args&&... args [[clang::lifetimebound]])
      : base{{std::move(fn), static_cast<Args&&>(args)...}} {}

 public:
  constexpr in_place_fn_args(ext::unsafe_move_args u, auto t)
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

using mut_bind_info = decltype([](auto bi) {
  bi.constness = ext::constness_t::mut;
  return bi;
});

using const_ref_bind_info = decltype([](auto bi) {
  bi.category = ext::category_t::ref;
  bi.constness = ext::constness_t::constant;
  return bi;
});

using mut_ref_bind_info = decltype([](auto bi) {
  bi.category = ext::category_t::ref;
  bi.constness = ext::constness_t::mut;
  return bi;
});

} // namespace detail

// `bind::in_place` and `bind::in_place_with` construct non-movable,
// non-copyable types in their final location.
//
// CAREFUL: As with other `bind::args{}`, the returned object stores references
// to `args` to avoid unnecessary move-copies.  A power user may wish to write
// a function that returns a binding, which OWNS some values.  For example, you
// may need to avoid a stack-use-after-free such as this one:
//
//   auto makeFoo(int n) { return bind::in_place<Foo>(n); } // BAD: `n` is dead
//
// To avoid the bug, either take `n` by-reference (often preferred), or store
// your values inside a `bind::in_place_with` callable:
//
//   auto makeFoo(int n) {
//     return bind::in_place_with([n]() { return Foo{n}; });
//   }
template <typename T, typename... Args>
constexpr auto in_place(Args&&... args [[clang::lifetimebound]]) {
  return detail::in_place_args<T, Args...>{static_cast<Args&&>(args)...};
}
// This is second-choice compared to `bind::in_place` because:
//   - Dangling references may be hidden inside `make_fn` captures --
//     `clang` offers no `lifetimebound` analysis for these (yet?).
//   - The type signature of the `in_place_args` includes a lambda.
// CAREFUL: While `make_fn` is taken by-value, `args` are stored as references,
// as in `bind::in_place`.
template <typename... Args>
constexpr auto in_place_with(
    auto make_fn, Args&&... args [[clang::lifetimebound]]) {
  return detail::in_place_fn_args<
      std::invoke_result_t<decltype(make_fn), Args&&...>,
      decltype(make_fn),
      Args...>{std::move(make_fn), static_cast<Args&&>(args)...};
}

// The below "binding modifiers" all return an immovable bound args list,
// meant to be passed only by-value, as a prvalue.  They can all take unary,
// or plural `like_args` as inputs.  Their only difference from
// `bind::args{values...}` is that these modifiers override some aspect of
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
//   (2) `constant{}` + `bind_to_storage_policy` stores the input as a `const`
//        value.  This allows you to move a non-const reference into a `const`
//        storage location.
//   (3) `constant{}` + `bind_as_argument()` passes a stored reference
//       via `std::as_const`.
//
// Specifiers can be overridden, e.g. you could (but should not!) express
// `mut_ref{}` as `mut{const_ref{}}`.
//
// There's currently no user-facing `by_ref{}`, which would leave the
// `constness` of the binding to be defaulted by each policy.  If a use-case
// arises, the test already contains its trivial implementation.

template <typename... Ts>
struct constant : ext::merge_update_args<detail::constant_bind_info, Ts...> {
  using ext::merge_update_args<detail::constant_bind_info, Ts...>::
      merge_update_args;
};
template <typename... Ts>
constant(Ts&&...) -> constant<ext::deduce_args_t<Ts>...>;

template <typename... Ts>
struct mut : ext::merge_update_args<detail::mut_bind_info, Ts...> {
  using ext::merge_update_args<detail::mut_bind_info, Ts...>::merge_update_args;
};
template <typename... Ts>
mut(Ts&&...) -> mut<ext::deduce_args_t<Ts>...>;

template <typename... Ts>
struct const_ref : ext::merge_update_args<detail::const_ref_bind_info, Ts...> {
  using ext::merge_update_args<detail::const_ref_bind_info, Ts...>::
      merge_update_args;
};
template <typename... Ts>
const_ref(Ts&&...) -> const_ref<ext::deduce_args_t<Ts>...>;

template <typename... Ts>
struct mut_ref : ext::merge_update_args<detail::mut_ref_bind_info, Ts...> {
  using ext::merge_update_args<detail::mut_ref_bind_info, Ts...>::
      merge_update_args;
};
template <typename... Ts>
mut_ref(Ts&&...) -> mut_ref<ext::deduce_args_t<Ts>...>;

// Future: Add `copied()` and `moved()` modifiers so the user can ensure
// pass-by-value with copy-, or move-copy semantics.  This enforcement already
// exists in `bind_to_storage_policy` and `category_t`.

} // namespace folly::bind

FOLLY_POP_WARNING
