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

#include <folly/functional/Invoke.h>

namespace folly {

namespace access {

/// size_fn
/// size
///
/// Invokes unqualified size with std::size in scope.
FOLLY_CREATE_FREE_INVOKER_SUITE(size, std);

/// empty_fn
/// empty
///
/// Invokes unqualified empty with std::empty in scope.
FOLLY_CREATE_FREE_INVOKER_SUITE(empty, std);

/// data_fn
/// data
///
/// Invokes unqualified data with std::data in scope.
FOLLY_CREATE_FREE_INVOKER_SUITE(data, std);

/// begin_fn
/// begin
///
/// Invokes unqualified begin with std::begin in scope.
FOLLY_CREATE_FREE_INVOKER_SUITE(begin, std);

/// end_fn
/// end
///
/// Invokes unqualified end with std::end in scope.
FOLLY_CREATE_FREE_INVOKER_SUITE(end, std);

} // namespace access

} // namespace folly
