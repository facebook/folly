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

#include <folly/Portability.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/CustomizationPoint.h>

namespace folly {

//  order_preserving_reinsertion_view_fn
//  order_preserving_reinsertion_view
//
//  Extension point for containers to provide an order such that if entries are
//  inserted into a new instance in that order, iteration order of the new
//  instance matches the original's. This can be useful for containers that have
//  defined but non-FIFO iteration order, such as F14Vector*.
//
//  Should return an iterable view (a type that provides begin() and end()).
//
//  Containers should provide overloads via tag-invoke.
struct order_preserving_reinsertion_view_fn {
 private:
  using fn = order_preserving_reinsertion_view_fn;

 public:
  template <typename Container>
  FOLLY_ERASE constexpr auto operator()(Container const& container) const
      noexcept(is_nothrow_tag_invocable_v<fn, Container const&>)
          -> tag_invoke_result_t<fn, Container const&> {
    return tag_invoke(*this, container);
  }
};
FOLLY_DEFINE_CPO(
    order_preserving_reinsertion_view_fn, order_preserving_reinsertion_view)

//  order_preserving_reinsertion_view_or_default_fn
//  order_preserving_reinsertion_view_or_default
//
//  If a tag-invoke extension of order_preserving_reinsertion_view is available
//  over the given argument, forwards to that. Otherwise, returns the argument.
struct order_preserving_reinsertion_view_or_default_fn {
 private:
  using fn = order_preserving_reinsertion_view_fn;

 public:
  template <
      typename Container,
      std::enable_if_t<is_tag_invocable_v<fn, Container const&>, int> = 0>
  FOLLY_ERASE constexpr auto operator()(Container const& container) const
      noexcept(is_nothrow_tag_invocable_v<fn, Container const&>)
          -> tag_invoke_result_t<fn, Container const&> {
    return tag_invoke(fn{}, container);
  }
  template <
      typename Container,
      std::enable_if_t<!is_tag_invocable_v<fn, Container const&>, int> = 0>
  FOLLY_ERASE constexpr Container const& operator()(
      Container const& container) const noexcept {
    return container;
  }
};
FOLLY_DEFINE_CPO(
    order_preserving_reinsertion_view_or_default_fn,
    order_preserving_reinsertion_view_or_default)

} // namespace folly
