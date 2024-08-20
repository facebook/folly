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

namespace folly::detail {

/// thread_is_dying
///
/// Queries whether the current thread is dying, as marked by companion function
/// thread_is_dying_mark.
///
/// Useful to avoid constructing non-trivially-destructible thread_local
/// variables, since they must later be destroyed.
[[nodiscard]] bool thread_is_dying();

/// thread_is_dying_mark
///
/// Marks the current thread as dying, to be queried by companion function
/// thread_is_dying.
void thread_is_dying_mark();

} // namespace folly::detail
