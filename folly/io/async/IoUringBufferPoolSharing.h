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

#include <folly/executors/IOThreadPoolExecutor.h>

namespace folly {

/**
 * Sets up zero-copy buffer pool sharing across EventBases in an
 * IOThreadPoolExecutorBase.
 *
 * This function should be called AFTER all EventBases are created and
 * their IoUringBackends are initialized. The first (numHwQueues - startQueueId)
 * EventBases are designated as owners — if they do not already have a
 * zero-copy buffer pool, one will be created via createZcBufferPool().
 * Owner pools are then exported and shared with the remaining EventBases
 * via import.
 *
 * If the number of zero-copy hardware queues >= the number of EventBases,
 * each backend gets its own pool and no sharing is needed.
 *
 * On any failure (e.g. a backend missing IoUringBackend, a failed pool
 * creation, or a failed import), this function will CHECK-fail to avoid
 * leaving things in a partially set up state.
 *
 * Example usage:
 *   folly::setupIoUringBufferPoolSharing(executor, numHwQueues, startQueueId);
 *
 * @param executor The IOThreadPoolExecutorBase whose EventBases will be set up
 *                 for buffer pool sharing. All EventBases must have
 *                 IoUringBackend.
 * @param numHwQueues Total number of hardware queues on the NIC.
 * @param startQueueId The first HW queue ID designated for zero copy. Queues
 *                     before this ID are reserved for non-zero-copy traffic.
 * @return true on success. CHECK-fails on any error.
 */
bool setupIoUringBufferPoolSharing(
    IOThreadPoolExecutorBase& executor,
    size_t numHwQueues,
    size_t startQueueId);

} // namespace folly
