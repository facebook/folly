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

// blocking affects senders

struct blocking_category {};

// trait & tag types

template <class... TN>
struct is_always_blocking;
template <>
struct is_always_blocking<> {
  using property_category = blocking_category;
};
template <class PS>
struct is_always_blocking<PS> : property_query<PS, is_always_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_always_blocking_v =
    is_always_blocking<PS>::value;

template <class... TN>
struct is_never_blocking;
template <>
struct is_never_blocking<> {
  using property_category = blocking_category;
};
template <class PS>
struct is_never_blocking<PS> : property_query<PS, is_never_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_never_blocking_v =
    is_never_blocking<PS>::value;

template <class... TN>
struct is_maybe_blocking;
template <>
struct is_maybe_blocking<> {
  using property_category = blocking_category;
};
template <class PS>
struct is_maybe_blocking<PS> : property_query<PS, is_maybe_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_maybe_blocking_v =
    is_maybe_blocking<PS>::value;

} // namespace pushmi
} // namespace folly
