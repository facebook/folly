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

#include <folly/detail/tuple.h>
#include <folly/lang/SafeAlias.h>
#include <folly/lang/bind/AsArgument.h>
#include <folly/lang/bind/Bind.h>
#include <folly/lang/bind/ToStorage.h>

///
/// See `SafeClosure.md` for more details.
///
/// `safe_closure()` reimplements a lambda-with-value-captures, exposing an API
/// analogous to Python's `functools.partial`.  These are better than a regular
/// lambda when you need to pass a callable into a `safe_alias`-aware API, such
/// as those in `folly/coro/safe`.
///
/// `safe_closure()` takes a *safe* callable -- a function pointer, a lambda
/// without captures, an empty callable class, or a type `T` with
///   strict_safe_alias_of_v<T> >= safe_alias::closure_min_arg_safety
///
/// Then, it measures the `safe_alias_of` its stored arguments, and marks the
/// returned callable accordingly.  This enables safe APIs to catch many
/// lifetime-safety bugs at compile time.
///
/// Passing an unsafe function or arguments is a compile error.  That's because
/// if you need an unsafe closure, a lambda with captures will do.
///

FOLLY_PUSH_WARNING
FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS

namespace folly {

namespace detail {

// Captures.h specializes this template to teach safe_closure to store
// capture<V> types as capture<V&>, without taking a direct dependency.
template <typename ST>
struct safe_closure_arg_storage_helper {
  // If we can't see the type, we probably can't see its specialization either.
  static_assert(require_sizeof<ST> >= 0);
  using type = ST;
};

// Transform a `binding_list_t` to `safe_closure` type parameters:
//   (1) a `vtag_t` of the input `bind_info_t`s, for use in `operator()`
//   (2) a tuple of by-value stored args.
// Constrain the type of `BI` to avoid slicing.
template <std::same_as<bind::ext::bind_info_t> auto... BIs, typename... BindTs>
auto deduce_safe_closure_params(
    tag_t<bind::ext::binding_t<BIs, BindTs>...> bindList)
    -> tag_t<
        vtag_t<BIs...>,
        lite_tuple::tuple<
            // This helper is a no-op for all types except `capture<Value>`,
            // for which it causes the ctor to perform an implicit conversion.
            typename safe_closure_arg_storage_helper<
                typename bind::ext::bind_to_storage_policy<
                    // Make fake `binding_t`s to always store args "by mutable
                    // value" -- storing refs would make the closure unsafe.
                    bind::ext::binding_t<bind::ext::bind_info_t{}, BindTs>>::
                    storage_type>::type...>>;
} // namespace detail

template <typename, typename>
class safe_closure;

template <
    typename Fn,
    // Constrain the type of `BI` to avoid slicing.
    std::same_as<bind::ext::bind_info_t> auto... ArgBIs,
    typename ArgsTup>
class safe_closure<Fn, tag_t<vtag_t<ArgBIs...>, ArgsTup>> {
 private:
  // This includes stateless, or explicitly safe functions.  The latter permits
  // nesting of `safe_closure`s.
  static_assert(
      strict_safe_alias_of_v<Fn> >= safe_alias::closure_min_arg_safety);
  static_assert(
      lenient_safe_alias_of_v<ArgsTup> >= safe_alias::closure_min_arg_safety);

  Fn fn_;
  ArgsTup argsTup_;

 public:
  safe_closure(auto bargs, Fn fn)
      : fn_{std::move(fn)}, argsTup_{[&bargs] {
          // This `apply` is here because `lite_tuple` currently lacks
          // converting constructors.
          return detail::lite_tuple::apply(
              [](auto&&... as) {
                return ArgsTup{static_cast<decltype(as)>(as)...};
              },
              std::move(bargs).unsafe_tuple_to_bind());
        }()} {}

  auto operator()(auto&&... unboundArgs) & {
    return detail::lite_tuple::apply(
        [&](auto&... as) {
          return fn_(
              bind::ext::bind_as_argument<ArgBIs>(as)...,
              static_cast<decltype(unboundArgs)>(unboundArgs)...);
        },
        argsTup_);
  }

  auto operator()(auto&&... unboundArgs) const& {
    return detail::lite_tuple::apply(
        [&](const auto&... as) {
          return std::as_const(fn_)(
              bind::ext::bind_as_argument<ArgBIs>(as)...,
              static_cast<decltype(unboundArgs)>(unboundArgs)...);
        },
        argsTup_);
  }

  auto operator()(auto&&... unboundArgs) && {
    return detail::lite_tuple::apply(
        [&](auto&&... as) {
          return std::move(fn_)(
              bind::ext::bind_as_argument<ArgBIs>(
                  static_cast<decltype(as)>(as))...,
              static_cast<decltype(unboundArgs)>(unboundArgs)...);
        },
        std::move(argsTup_));
  }

  // Use lenient safety for the stored arguments since we have no a priori
  // expectation of them being high-risk. Contrast with this recursive form:
  //   safe_alias_of_pack<Default, Fn, ArgsTup>
  // With that, passing `SomeArg` with no safety marking would make the whole
  // closure strict-unsafe, which would degrade usability.
  template <safe_alias>
  using folly_private_safe_alias_t = safe_alias_constant<std::min(
      strict_safe_alias_of_v<Fn>, lenient_safe_alias_of_v<ArgsTup>)>;
};

template <typename BindArgs, typename Fn>
safe_closure(BindArgs, Fn)
    -> safe_closure<
        Fn,
        decltype(detail::deduce_safe_closure_params(
            typename BindArgs::binding_list_t{}))>;

} // namespace folly

FOLLY_POP_WARNING
