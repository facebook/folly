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

#include <folly/experimental/pushmi/properties.h>

namespace folly {
namespace pushmi {

// sequence affects executors

struct sequence_category {};

template <class... TN>
struct is_fifo_sequence;
template <>
struct is_fifo_sequence<> {
  using property_category = sequence_category;
};
template <class PS>
struct is_fifo_sequence<PS> : property_query<PS, is_fifo_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_fifo_sequence_v =
    is_fifo_sequence<PS>::value;


template <class... TN>
struct is_concurrent_sequence;
template <>
struct is_concurrent_sequence<> {
  using property_category = sequence_category;
};
template <class PS>
struct is_concurrent_sequence<PS>
    : property_query<PS, is_concurrent_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_concurrent_sequence_v =
    is_concurrent_sequence<PS>::value;

} // namespace pushmi
} // namespace folly
