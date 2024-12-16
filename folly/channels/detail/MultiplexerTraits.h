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

#include <folly/Traits.h>
#include <folly/functional/traits.h>

namespace folly {
namespace channels {
namespace detail {

template <typename MultiplexerType>
struct MultiplexerTraits {
  using OnNewSubscriptionPtr = decltype(&MultiplexerType::onNewSubscription);
  using OnNewSubscriptionTraits = function_traits<
      typename member_pointer_traits<OnNewSubscriptionPtr>::member_type>;

  using OnInputValuePtr = decltype(&MultiplexerType::onInputValue);
  using OnInputValueTraits = function_traits<
      typename member_pointer_traits<OnInputValuePtr>::member_type>;

  // First parameter type of MultiplexerType::onNewSubscription
  using KeyType = typename OnNewSubscriptionTraits::template argument<0>;

  // Second parameter type for MultiplexerType::onNewSubscription
  using KeyContextType =
      std::decay_t<typename OnNewSubscriptionTraits::template argument<1>>;

  // Third parameter type for MultiplexerType::onNewSubscription
  using SubscriptionArgType =
      typename OnNewSubscriptionTraits::template argument<2>;

  // First parameter value type of MultiplexerType::onInputValue
  using InputValueType =
      typename OnInputValueTraits::template argument<0>::element_type;

  // Element type of the returned vector from MultiplexerType::onNewSubscription
  using OutputValueType =
      typename OnNewSubscriptionTraits::result_type::StorageType::value_type;
};
} // namespace detail
} // namespace channels
} // namespace folly
