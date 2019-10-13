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

#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/pushmi/customizations/Executor.h>
#include <folly/experimental/pushmi/executor/executor.h>

namespace folly {

// a derived class can disable the traits definitions by adding the following
//
// namespace pushmi {
// template <class T>
// struct property_set_traits_disable<
//     T,
//     ::folly::SequencedExecutor,
//     typename std::enable_if<
//         std::is_base_of<Derived, T>::value>::type> : std::true_type {
// };
// } // namespace pushmi
//

namespace pushmi {

template <class T>
struct property_set_traits_disable<
    T,
    ::folly::Executor,
    typename std::enable_if<
        std::is_base_of<::folly::SequencedExecutor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::SequencedExecutor>>::type>
    : std::true_type {};

template <class T>
struct property_set_traits<
    ::folly::Executor::KeepAlive<T>,
    typename std::enable_if<
        std::is_base_of<::folly::SequencedExecutor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::SequencedExecutor>>::type> {
  using properties = property_set<is_fifo_sequence<>>;
};

template <class T>
struct property_set_traits<
    ::folly::ExecutorTask<T>,
    typename std::enable_if<
        std::is_base_of<::folly::SequencedExecutor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::SequencedExecutor>>::type> {
  using properties = property_set<is_maybe_blocking<>>;
};

} // namespace pushmi

} // namespace folly
