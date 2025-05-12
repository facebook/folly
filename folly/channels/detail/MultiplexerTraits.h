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
  using OnNewSubscriptionSig =
      member_pointer_member_t<decltype(&MultiplexerType::onNewSubscription)>;

  using OnInputValueSig =
      member_pointer_member_t<decltype(&MultiplexerType::onInputValue)>;

  // First parameter type of MultiplexerType::onNewSubscription
  using KeyType = function_arguments_element_t<0, OnNewSubscriptionSig>;

  // Second parameter type for MultiplexerType::onNewSubscription
  using KeyContextType =
      std::decay_t<function_arguments_element_t<1, OnNewSubscriptionSig>>;

  // Third parameter type for MultiplexerType::onNewSubscription
  using SubscriptionArgType =
      function_arguments_element_t<2, OnNewSubscriptionSig>;

  // First parameter value type of MultiplexerType::onInputValue
  using InputValueType =
      typename function_arguments_element_t<0, OnInputValueSig>::element_type;

  // Element type of the returned vector from MultiplexerType::onNewSubscription
  using OutputValueType =
      typename function_result_t<OnNewSubscriptionSig>::StorageType::value_type;
};
} // namespace detail
} // namespace channels
} // namespace folly
