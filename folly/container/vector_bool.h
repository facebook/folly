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

#include <memory>
#include <vector>

#include <folly/container/detail/BoolWrapper.h>
#include <folly/memory/MemoryResource.h>

namespace folly {

/// Convenience alias to use instead of `std::vector<bool>` to avoid infamous
/// `std::vector<bool>` specialization.
///
/// Usage example:
///
///     folly::vector_bool<> vec = {false, true};
///     assert(vec[0] == false);
///     assert(vec[1] == true);
template <template <class> typename Allocator = std::allocator>
using vector_bool = std::vector<
    folly::detail::BoolWrapper, //
    Allocator<folly::detail::BoolWrapper>>;

#if FOLLY_HAS_MEMORY_RESOURCE
namespace pmr {

using vector_bool = vector_bool<std::pmr::polymorphic_allocator>;

} // namespace pmr
#endif // FOLLY_HAS_MEMORY_RESOURCE

} // namespace folly
