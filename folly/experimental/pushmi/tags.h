/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/pushmi/detail/sender_base.h>

namespace folly {
namespace pushmi {
namespace detail {
// inherit: a class that inherits from a bunch of bases
template <class... Ts>
struct inherit : Ts... {
  inherit() = default;
  constexpr inherit(Ts... ts)
    : Ts((Ts&&) ts)...
  {}
};
template <class T>
struct inherit<T> : T {
  inherit() = default;
  explicit constexpr inherit(T t)
    : T((T&&) t)
  {}
};
template <>
struct inherit<>
{};
} // namespace detail

// derive from this for types that need to find operator|() overloads by ADL

struct pipeorigin {};

// traits & tags
struct sender_tag
  : sender_base<sender_tag> {
};
namespace detail {
struct virtual_sender_tag
  : virtual sender_tag {
};
} // namespace detail

struct single_sender_tag
  : sender_base<single_sender_tag, detail::virtual_sender_tag> {
};

struct flow_sender_tag
  : sender_base<flow_sender_tag, detail::virtual_sender_tag> {
};
struct flow_single_sender_tag
  : sender_base<
      flow_single_sender_tag,
      detail::inherit<flow_sender_tag, single_sender_tag>> {
};

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

// blocking affects senders

struct blocking_category {};

// sequence affects senders

struct sequence_category {};

// trait & tag types

template <class... TN>
struct is_flow;
template <>
struct is_flow<> {
  using property_category = flow_category;
};

template <class... TN>
struct is_receiver;
template <>
struct is_receiver<> {
  using property_category = receiver_category;
};

template <class... TN>
struct is_always_blocking;
template <>
struct is_always_blocking<> {
  using property_category = blocking_category;
};

template <class... TN>
struct is_never_blocking;
template <>
struct is_never_blocking<> {
  using property_category = blocking_category;
};

template <class... TN>
struct is_maybe_blocking;
template <>
struct is_maybe_blocking<> {
  using property_category = blocking_category;
};

template <class... TN>
struct is_fifo_sequence;
template <>
struct is_fifo_sequence<> {
  using property_category = sequence_category;
};

template <class... TN>
struct is_concurrent_sequence;
template <>
struct is_concurrent_sequence<> {
  using property_category = sequence_category;
};

} // namespace pushmi
} // namespace folly
