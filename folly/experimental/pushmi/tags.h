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

namespace folly {
namespace pushmi {

// derive from this for types that need to find operator|() overloads by ADL

struct pipeorigin {};

// cardinality affects both sender and receiver

struct cardinality_category {};

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// blocking affects senders

struct blocking_category {};

// sequence affects senders

struct sequence_category {};

// trait & tag types

template <class... TN>
struct is_single;
template <>
struct is_single<> {
  using property_category = cardinality_category;
};

template <class... TN>
struct is_many;
template <>
struct is_many<> {
  using property_category = cardinality_category;
};

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
struct is_sender;
template <>
struct is_sender<> {
  using property_category = sender_category;
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
