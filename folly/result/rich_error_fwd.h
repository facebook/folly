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

#include <folly/Portability.h> // FOLLY_HAS_RESULT

#if FOLLY_HAS_RESULT

namespace folly {

class rich_error_base;

template <typename>
class rich_error;

class rich_exception_ptr;

// DO NOT add random details here, see `result/detail/rich_error_common.h` e.g.
namespace detail {
// `std::exception` isn't constexpr in C++20, so there's no constexpr
// `rich_error<T>`, but only `immortal_rich_error_storage<T>`.
//
// This passkey protects the ctor used to create leaky singletons of
// `rich_error<T>` at runtime, for `get_exception` queries that require them.
// As a result, immortal errors act as-if they **are** `std::exception`s.
struct immortal_rich_error_private_t {};
} // namespace detail

} // namespace folly

#endif // FOLLY_HAS_RESULT
