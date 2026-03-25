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

#include <folly/Executor.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/**
 * Sets up zero-copy buffer pool creation and sharing across EventBases.
 *
 * This function should be called AFTER all EventBases are created and
 * their IoUringBackends are initialized. The first
 * N hwQueues EventBases are designated as owners — if
 * they do not already have a zero-copy buffer pool, one will be created
 * via createZcBufferPool(). Owner pools are then exported and shared with
 * the remaining EventBases via import.
 *
 * If the number of zero-copy hardware queues >= the number of EventBases,
 * each backend gets its own pool and no sharing is needed.
 *
 * @param eventBases The EventBases to set up for buffer pool sharing.
 *                   All must have IoUringBackend.
 * @param numHwQueues hw queues allocated for zcrx
 * @return true on success. CHECK-fails on any error.
 */
bool setupIoUringBufferPoolSharing(
    std::vector<folly::EventBase*>& eventBases, size_t numHwQueues);

template <typename T>
bool setupIoUringBufferPoolSharing(
    std::vector<T>& eventBases, size_t numHwQueues) {
  std::vector<folly::EventBase*> evbPtrs;
  evbPtrs.reserve(eventBases.size());
  for (auto& eb : eventBases) {
    evbPtrs.push_back(eb.get());
  }
  return setupIoUringBufferPoolSharing(evbPtrs, numHwQueues);
}

} // namespace folly
