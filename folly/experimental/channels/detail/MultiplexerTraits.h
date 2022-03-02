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

#include <folly/experimental/channels/detail/FunctionTraits.h>

namespace folly {
namespace channels {
namespace detail {

template <typename MultiplexerType>
struct MultiplexerTraits {
  // First parameter type of MultiplexerType::onNewSubscription
  using KeyType = std::tuple_element_t<
      0,
      typename FunctionTraits<
          decltype(&MultiplexerType::onNewSubscription)>::Args>;

  // Second parameter type for MultiplexerType::onNewSubscription
  using KeyContextType = std::decay_t<typename std::tuple_element_t<
      1,
      typename FunctionTraits<
          decltype(&MultiplexerType::onNewSubscription)>::Args>>;

  // Third parameter type for MultiplexerType::onNewSubscription
  using SubscriptionArgType = std::tuple_element_t<
      2,
      typename FunctionTraits<
          decltype(&MultiplexerType::onNewSubscription)>::Args>;

  // First parameter value type of MultiplexerType::onInputValue
  using InputValueType = typename std::tuple_element_t<
      0,
      typename FunctionTraits<decltype(&MultiplexerType::onInputValue)>::Args>::
      element_type;

  // Element type of the returned vector from MultiplexerType::onNewSubscription
  using OutputValueType =
      typename FunctionTraits<decltype(&MultiplexerType::onNewSubscription)>::
          Return::StorageType::value_type;
};
} // namespace detail
} // namespace channels
} // namespace folly
