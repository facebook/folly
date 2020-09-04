/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <type_traits>
#include <utility>

#include <folly/Traits.h>
#include <folly/functional/Invoke.h>

/**
 * folly implementation of `std::overload` like functionality
 *
 * Example:
 *  struct One {};
 *  struct Two {};
 *  boost::variant<One, Two> value;
 *
 *  variant_match(value,
 *    [] (const One& one) { ... },
 *    [] (const Two& two) { ... });
 */

namespace folly {

namespace detail {
template <typename...>
struct Overload;

template <typename Case, typename... Cases>
struct Overload<Case, Cases...> : Overload<Cases...>, Case {
  Overload(Case c, Cases... cs)
      : Overload<Cases...>(std::move(cs)...), Case(std::move(c)) {}

  using Case::operator();
  using Overload<Cases...>::operator();
};

template <typename Case>
struct Overload<Case> : Case {
  explicit Overload(Case c) : Case(std::move(c)) {}

  using Case::operator();
};
} // namespace detail

/*
 * Combine multiple `Cases` in one function object
 */
template <typename... Cases>
decltype(auto) overload(Cases&&... cases) {
  return detail::Overload<typename std::decay<Cases>::type...>{
      std::forward<Cases>(cases)...};
}

namespace overload_detail {
FOLLY_CREATE_MEMBER_INVOKER(valueless_by_exception, valueless_by_exception);
FOLLY_CREATE_FREE_INVOKER(visit, visit);
FOLLY_CREATE_FREE_INVOKER(apply_visitor, apply_visitor);
} // namespace overload_detail

/*
 * Match `Variant` with one of the `Cases`
 *
 * Note: you can also use `[] (const auto&) {...}` as default case
 *
 * Selects `visit` if `v.valueless_by_exception()` available and the call to
 * `visit` is valid (e.g. `std::variant`). Otherwise, selects `apply_visitor`
 * (e.g. `boost::variant`, `folly::DiscriminatedPtr`).
 */
template <typename Variant, typename... Cases>
decltype(auto) variant_match(Variant&& variant, Cases&&... cases) {
  using invoker = std::conditional_t<
      folly::Conjunction<
          is_invocable<overload_detail::valueless_by_exception, Variant>,
          is_invocable<
              overload_detail::visit,
              decltype(overload(std::forward<Cases>(cases)...)),
              Variant>>::value,
      overload_detail::visit,
      overload_detail::apply_visitor>;
  return invoker{}(
      overload(std::forward<Cases>(cases)...), std::forward<Variant>(variant));
}

} // namespace folly
