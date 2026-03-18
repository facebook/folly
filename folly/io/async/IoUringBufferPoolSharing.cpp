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

#include <folly/io/async/IoUringBufferPoolSharing.h>

#include <algorithm>

#include <glog/logging.h>

#include <folly/Function.h>
#include <folly/io/async/Liburing.h>

#if FOLLY_HAS_LIBURING
#include <folly/io/async/IoUringBackend.h>
#endif

namespace folly {

namespace {

bool setupIoUringBufferPoolSharingImpl(
    size_t numIoThreads,
    folly::FunctionRef<folly::EventBase*(size_t)> getEventBase,
    size_t numHwQueues) {
#if !FOLLY_HAS_LIBURING
  (void)numIoThreads;
  (void)getEventBase;
  (void)numHwQueues;
  LOG(FATAL) << "Buffer pool sharing is only supported on Linux";
#else
  CHECK_GT(numIoThreads, 0) << "need at least one IO thread";
  std::vector<IoUringBackend*> backends;
  backends.reserve(numIoThreads);
  for (size_t i = 0; i < numIoThreads; ++i) {
    auto* backend =
        dynamic_cast<IoUringBackend*>(getEventBase(i)->getBackend());
    CHECK(backend) << "EventBase at index " << i
                   << " does not have IoUringBackend";
    backends.push_back(backend);
  }

  size_t numOwners = std::min(numHwQueues, backends.size());
  for (size_t i = 0; i < numOwners; ++i) {
    if (!backends[i]->zcBufferPool()) {
      CHECK(backends[i]->createZcBufferPool())
          << "Failed to create zero-copy buffer pool for EventBase at index "
          << i;
    }
  }

  if (numHwQueues >= backends.size()) {
    // Every backend has its own HW queue, no sharing needed.
    return true;
  }

  for (size_t i = numHwQueues; i < backends.size(); ++i) {
    size_t ownerIdx = (i - numHwQueues) % numOwners;
    auto handle = backends[ownerIdx]->exportZcBufferPool();
    CHECK(backends[i]->importZcBufferPool(std::move(handle)))
        << "Failed to import buffer pool handle into EventBase at index " << i
        << " from owner at index " << ownerIdx;
  }

  return true;
#endif
}

} // namespace

bool setupIoUringBufferPoolSharing(
    std::vector<std::unique_ptr<folly::EventBase>>& eventBases,
    size_t numHwQueues) {
  return setupIoUringBufferPoolSharingImpl(
      eventBases.size(),
      [&](size_t i) { return eventBases[i].get(); },
      numHwQueues);
}

bool setupIoUringBufferPoolSharing(
    std::vector<folly::EventBase*>& eventBases, size_t numHwQueues) {
  return setupIoUringBufferPoolSharingImpl(
      eventBases.size(), [&](size_t i) { return eventBases[i]; }, numHwQueues);
}

} // namespace folly
