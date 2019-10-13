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

#include <chrono>
#include <cstddef>
#include <exception>

#include <folly/experimental/pushmi/detail/traits.h>

namespace folly {
namespace pushmi {

// derive from this for types that need to find operator|() overloads by ADL
struct pipeorigin {};

// properties types:

template <class T, class = void>
struct property_traits;

template <class T, class = void>
struct property_set_traits;

template <class... PropertyN>
struct property_set;

template <class T, class Target, class = void>
struct property_set_traits_disable;

// Traits types:

template <class T, class = void>
struct sender_traits;

template <class T, class = void>
struct receiver_traits;

// implementation types

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class executor;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class constrained_executor;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_executor;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_sender;

template <class E = std::exception_ptr, class... VN>
class any_receiver;

template <
    class PE = std::exception_ptr,
    class PV = std::ptrdiff_t,
    class E = PE,
    class... VN>
class any_flow_receiver;

template <class E = std::exception_ptr, class... VN>
class any_single_sender;

template <class E = std::exception_ptr, class... VN>
class any_sender;

template <class PE = std::exception_ptr, class E = PE, class... VN>
class any_flow_single_sender;

template <
    class PE = std::exception_ptr,
    class PV = std::ptrdiff_t,
    class E = PE,
    class... VN>
class any_flow_sender;

template <class E = std::exception_ptr>
class any_executor;

template <class E = std::exception_ptr>
struct any_executor_ref;

template <class E = std::exception_ptr, class CV = std::ptrdiff_t>
class any_constrained_executor;

template <class E = std::exception_ptr, class TP = std::ptrdiff_t>
struct any_constrained_executor_ref;

template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
class any_time_executor;

template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
struct any_time_executor_ref;

namespace operators {}
namespace extension_operators {}
namespace aliases {
namespace v = ::folly::pushmi;
namespace mi = ::folly::pushmi;
namespace op = ::folly::pushmi::operators;
namespace ep = ::folly::pushmi::extension_operators;
} // namespace aliases

namespace detail {
struct any {
  template <class T>
  constexpr any(T&&) noexcept {}
};
} // namespace detail

namespace awaitable_senders {
// Used in the definition of sender_traits to define Senders in terms
// Awaitables without causing constraint recursion.
std::false_type safe_to_test_awaitable(void*);
struct sender_adl_hook;
} // namespace awaitable_senders

template<template <class...> class T>
struct construct_deduced;

template<>
struct construct_deduced<receiver>;

template<>
struct construct_deduced<flow_receiver>;

template<>
struct construct_deduced<single_sender>;

template<>
struct construct_deduced<sender>;

template<>
struct construct_deduced<flow_single_sender>;

template<>
struct construct_deduced<flow_sender>;

} // namespace pushmi
} // namespace folly
