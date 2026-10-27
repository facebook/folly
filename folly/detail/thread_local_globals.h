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

#include <folly/portability/PThread.h>

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

/// thread_local_register_pthread_key
///
/// Records a pthread key created by folly, to be deleted by
/// thread_local_delete_pthread_keys.
void thread_local_register_pthread_key(pthread_key_t key);

/// thread_local_delete_pthread_keys
///
/// Deletes the pthread keys folly has created, so that a shared object
/// containing folly can be unloaded by dlclose while threads that used folly
/// thread-locals are still running. Those threads would otherwise call the
/// keys' destructors, which are in the unloaded code, when they exit. Their
/// thread-local values are leaked instead.
///
/// Call only immediately before this copy of folly is unloaded: nothing may use
/// a folly thread-local afterwards, in any thread. In particular, do not call
/// it during process exit, when other threads may still be using them.
///
/// Deletes only the keys this copy of folly created. Do not call it if another
/// shared object may share this copy's thread-locals, as it can when folly's
/// symbols are exported and createGlobal deduplicates singletons across shared
/// objects; that object's keys would be deleted from under it.
void thread_local_delete_pthread_keys();

} // namespace folly::detail
