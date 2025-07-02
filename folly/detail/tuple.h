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
#include <folly/Traits.h>
#include <folly/lang/SafeAlias-fwd.h>

/// Any file constructing `lite_tuple` should contain this after `#include`s:
///   FOLLY_PUSH_WARNINGS
///   FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS
/// Followed by `FOLLY_POP_WARNING` at the bottom.  The reason is that
/// `-Wmissing-braces` otherwise breaks deduction guides, and makes aggregate
/// initialization a real headache to use:
///   lite_tuple::tuple t{1, 2, 3}; // before :)
///   lite_tuple::tuple<int> t{{{1}, {2}, {3}}}; // after :'(
///
/// At the same time, I don't want to give up on having it be an aggregate type,
/// because (1) aggregate types should get copy elision when initialized from
/// prvalues -- but a perfect-forwarding constructor would break that, and (2)
/// not being an aggregate type **might** break register-pass optimizations.
///
/// So, instead I replace `-Wmissing-braces` by `-Wmissing-field-initializers`,
/// which has its correctness benefits without any of the bad side effects.
///
/// FIXME: Remove when our warnings are fixed to be this way globally.
#define FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS \
  FOLLY_GNU_DISABLE_WARNING("-Wmissing-braces") \
  FOLLY_GNU_ENABLE_WARNING("-Wmissing-field-initializers")

FOLLY_PUSH_WARNING
FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS

/// A fast, incomplete `std::tuple` mimic with left-to-right construction
/// ordering, for use in `folly/` internals.
///
/// IMPORTANT: Treat the class members of `entry` and `tuple_base` as PRIVATE,
/// these implementation details are subject to change.  Only the `std::tuple`
/// mimic APIs are supported as public.
///
/// BEWARE: This is NOT a drop-in `std::tuple` replacement. Some differences:
///
///   - `lite_tuple` prioritizes a minimal, fast-to-compile implementation, and
///     so it lacks many `std::tuple` features.  For example, there are no
///     converting constructors or comparators at present.
///
///   - NEW FEATURES SHOULD BE ADDED SPARINGLY -- at a minimum, benchmark the
///     build of `//folly/coro/safe/...` (especially `test:async_closure_test`)
///     to make sure there's no significant regression.
///
///   - Unlike `std::tuple`, `lite_tuple::tuple`:
///       * Has a specified construction & destruction order -- left-to-right,
///         of course.  Use curly-brace init to also order argument evaluation.
///       * Is an aggregate type, which can provide better build & runtime
///         performance.  Do not break this contract!
///       * Is a structural type, but prefer `vtag_t` for most metaprogramming.
///       * Likely has various minor differences of behavior, both due to
///         aggregate initialization, and just overall lack of broad use.
///         There's one such known caveat (see FIXME in the test).
///
/// That said, this IS an `std::tuple` mimic, and its test checks for identical
/// behavior across a significant number of APIs.  Notably, `lite_tuple`:
///   - Can store values, lvalue & rvalue references.
///   - Has `get`, `tuple_cat`, `apply`, and `forward_as_tuple`.
///   - Supports `std::tuple_size_v`, `std::tuple_element_t`, and therefore
///     structured binding.
///
/// ## Why isn't this a public API?
///
/// Since the C++ committee has historically been unable to plan for ABI breaks,
/// `std::tuple` will not get better in the foreseeable future, neither in terms
/// of ordering, nor in terms of build speed.  For this reason, you MAY find it
/// reasonable to graduate `lite_tuple` to a public `folly` API.  However, to
/// make this a plausible idea, be prepared to put in much more work:
///   - Hyrum's law will codify the implementation's actual behavior as
///     contract.  To make this less problematic, you MUST add dramatically more
///     API testing, closely following existing compliance tests for
///     `std::tuple` (plus `tuplet` where appropriate) and clearly calling out
///     where we deviate.
///   - Put more effort into testing that stuff that had better not compile
///     actually doesn't compile (see the rvalue FIXME, e.g.).
///   - Implement runtime and build-time benchmarks, to help prevent
///     future attempts at API bloat from regressing key metrics.
///   - Document API extensions we MAY versus MUST NOT do.
///
///   - If you get a proper "good tuple" landed in `folly/` (or a dependency),
///     that supersedes this I would be EXCITED to delete this code.  This is
///     only used in internal implementation details, so it's easy to swap.
///
/// ## Prior art
///
/// The idea of an aggregate-type tuple is pretty old, and I am not sure where
/// I've seen it first.  Some uses of this pattern:
///   - `codeinred/tuplet` is a pretty nice standalone library, with an emphasis
///     on runtime & build speed.  It is not used in `folly/` for two reasons:
///     (a) it wouldn't pass the `lite_tuple` suite due to significant bugs in
///     its rvalue reference support, (b) `folly` is cautious with taking deps,
///     (c) it implements far more API than we need, and API bloat has costs.
///   - `NVIDIA/stdexec/__detail/__tuple.hpp` is another notable example.
///   - In 2024 (well after other such types), but before seeing either of the
///     above, I wrote a named tuple called `type_idx_map`, using the same
///     patterns.  The code wasn't committed, but is here for posterity:
///     https://gist.github.com/snarkmaster/cfa209a4d05104a78769ca8062f382a0

namespace folly::detail::lite_tuple {

namespace detail {

template <size_t I, typename T>
struct entry {
  using entry_type = T;
  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] T entry_value;
  constexpr auto operator<=>(const entry&) const = default;
};

template <typename Seq, typename...>
struct tuple_base;

template <size_t... Is, typename... Ts>
struct tuple_base<std::index_sequence<Is...>, Ts...> : entry<Is, Ts>... {
  using tuple_base_list = tag_t<entry<Is, Ts>...>;
  constexpr auto operator<=>(const tuple_base&) const = default;
};

} // namespace detail

template <typename... Ts>
struct tuple : detail::tuple_base<std::index_sequence_for<Ts...>, Ts...> {
  constexpr auto operator<=>(const tuple&) const = default;
};
template <typename... Ts>
tuple(Ts...) -> tuple<Ts...>;

template <size_t I, typename T>
FOLLY_ALWAYS_INLINE constexpr T& get(detail::entry<I, T>& tup) noexcept {
  return tup.entry_value;
}
template <size_t I, typename T>
FOLLY_ALWAYS_INLINE constexpr const T& get(
    const detail::entry<I, T>& tup) noexcept {
  return tup.entry_value;
}
template <size_t I, typename T>
FOLLY_ALWAYS_INLINE constexpr decltype(auto) get(
    detail::entry<I, T>&& tup) noexcept {
  using dst = decltype(static_cast<decltype(tup)>(tup).entry_value);
  return static_cast<dst&&>(tup.entry_value);
}

FOLLY_ALWAYS_INLINE constexpr auto forward_as_tuple(auto&&... a) noexcept {
  return tuple<decltype(a)&&...>{static_cast<decltype(a)>(a)...};
}

FOLLY_ALWAYS_INLINE constexpr decltype(auto) apply(auto&& fn, auto&& tup) {
  using tupv = std::remove_reference_t<decltype(tup)>;
  return [&]<size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
    return static_cast<decltype(fn)>(fn)(
        get<Is>(static_cast<decltype(tup)>(tup))...);
  }(std::make_index_sequence<std::tuple_size_v<tupv>>{});
}

FOLLY_ALWAYS_INLINE constexpr decltype(auto) reverse_apply(
    auto&& fn, auto&& tup) {
  using tupv = std::remove_reference_t<decltype(tup)>;
  constexpr size_t Last = std::tuple_size_v<tupv> - 1;
  return [&]<size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
    return static_cast<decltype(fn)>(fn)(
        get<Last - Is>(static_cast<decltype(tup)>(tup))...);
  }(std::make_index_sequence<Last + 1>{});
}

// `tuple_cat` implementation details.  Credit: This follows the `tuplet`
// algorithm, which in turn appears to derive from Eric Niebler's
// `tuple_cat.cpp` -- read its docs for another explanation:
// https://github.com/ericniebler/meta/blob/master/example/tuple_cat.cpp
//
// Note that I did not keep `tuplet`'s GCC optimizations.
//
// NB: To be `folly`-idiomatic, this uses `type_list_concat_t`, which uses
// recursive template instantiation.  This might compile slower than `tuplet`,
// which uses fold expressions, but...  done is better than perfect here.
namespace detail {

// Return a `tag_t` containing `sizeof...(ForEach)` copies of `T`
template <typename T, typename... ForEach>
consteval auto repeat_type(tag_t<ForEach...>) {
  return tag<type_t<T, ForEach>...>;
}

// `Base` is a `struct entry` base class of a tuple-of-tuples.  Returns the
// `tuple_base_list` of the inner tuple "indexed" by this `Base`.
template <typename Base>
using inner_tuple_base_list_t = typename std::remove_reference_t<
    typename Base::entry_type>::tuple_base_list;

// Given the bases of some `outerTuples...`, return a `tag_t` repeating the
// corresponding base for each of the tuple's entries (cardinality of
// `tuple_cat(outerTuples...)`).
//
// Concretely: If `B1` comes from `tuple<int, char>`, and `B2` from
// `tuple<float>`, then the result is `tag_t<B1, B1, B2>`.
template <typename... Bases>
consteval auto outer_base_type_for_each_concat_entry(tag_t<Bases...>) {
  return type_list_concat_t<
      tag_t,
      decltype(repeat_type<Bases>(inner_tuple_base_list_t<Bases>{}))...>{};
}

// Given the bases of some `outerTuples...`, return a `tag_t` containing the
// "inner" base corresponding to each of the tuple's entries.
//
// IMPORTANT: Before looking up by inner base, one must `static_cast` to the
// correct outer base -- otherwise, ambiguity would ensue when the same inner
// base occurs in more than one outer tuple.
template <typename... Bases>
consteval auto inner_base_type_for_each_concat_entry(tag_t<Bases...>) {
  return type_list_concat_t<tag_t, inner_tuple_base_list_t<Bases>...>{};
}

// `Outer` and `Inner` together form the base-class "index" for each entry in
// the `tuple_cat` output.  To avoid ambiguity, we have to first resolve by the
// `Outer` base, then by the `Inner` base to get the input tuple element.
template <typename... Outer, typename... Inner>
constexpr auto tuple_cat_impl(
    auto tup_of_tup_refs, tag_t<Outer...>, tag_t<Inner...>)
    -> tuple<typename Inner::entry_type...> {
  return {
      // These two casts & member accesses are meant as a build-time
      // optimization so as to avoid using `get<>`.  I **think** the value
      // category mapping is correct, but please suggest more tests.
      //
      // Needed if the destination type is an rref: check_tuple_cat_refs()
      static_cast<typename Inner::entry_type>(
          // Needed if a source tuple is by-rvalue: check_tuple_cat_move()
          static_cast<typename Outer::entry_type&&>(
              tup_of_tup_refs.Outer::entry_value)
              .Inner::entry_value)...};
}

} // namespace detail

template <typename... Tups>
FOLLY_ALWAYS_INLINE constexpr auto tuple_cat(Tups&&... tups) {
  constexpr auto bases = typename tuple<Tups&&...>::tuple_base_list{};
  return detail::tuple_cat_impl(
      tuple<Tups&&...>{static_cast<Tups&&>(tups)...},
      // Base class "indexes" into the tuple-of-tuples.  We'll walk through
      // them in lockstep, querying (outer, inner) to pull out each entry.
      detail::outer_base_type_for_each_concat_entry(bases),
      detail::inner_base_type_for_each_concat_entry(bases));
}

} // namespace folly::detail::lite_tuple

namespace folly {
template <typename... As>
struct safe_alias_of<::folly::detail::lite_tuple::tuple<As...>>
    : detail::safe_alias_of_pack<As...> {};
} // namespace folly

namespace std {

template <typename... Ts>
struct tuple_size<::folly::detail::lite_tuple::tuple<Ts...>>
    : std::integral_constant<size_t, sizeof...(Ts)> {};

template <size_t I, typename... Ts>
struct tuple_element<I, ::folly::detail::lite_tuple::tuple<Ts...>> {
  using type = ::folly::type_pack_element_t<I, Ts...>;
};

} // namespace std

FOLLY_POP_WARNING
