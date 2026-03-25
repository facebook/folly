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
#include <optional>

#include <glog/logging.h>

#include <folly/Function.h>
#include <folly/io/async/Liburing.h>

#if FOLLY_HAS_LIBURING
#include <folly/io/async/IoUringBackend.h>
#endif

namespace folly {

namespace {

#if FOLLY_HAS_LIBURING
struct EbBackend {
  folly::EventBase* evb;
  IoUringBackend* backend;
};

bool compareByZcRxQueueId(const EbBackend& a, const EbBackend& b) {
  auto aq = a.backend->options().zcRxQueueId;
  auto bq = b.backend->options().zcRxQueueId;
  if (aq < 0 && bq < 0) {
    return false;
  }
  if (aq < 0) {
    return false;
  }
  if (bq < 0) {
    return true;
  }
  return aq < bq;
}
#endif

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
  CHECK_GT(numHwQueues, 0)
      << "need at least 1 hw queue but passing: " << numHwQueues;

  std::vector<EbBackend> entries;
  entries.reserve(numIoThreads);
  for (size_t i = 0; i < numIoThreads; ++i) {
    auto* evb = getEventBase(i);
    auto* backend = dynamic_cast<IoUringBackend*>(evb->getBackend());
    CHECK(backend) << "EventBase at index " << i
                   << " does not have IoUringBackend";
    entries.push_back({evb, backend});
  }

  std::sort(entries.begin(), entries.end(), compareByZcRxQueueId);

  size_t numOwners = std::min(numHwQueues, entries.size());

  for (size_t i = 0; i < numOwners; ++i) {
    auto* evb = entries[i].evb;
    auto* backend = entries[i].backend;
    evb->runInEventBaseThreadAndWait([backend, i] {
      if (!backend->zcBufferPool()) {
        CHECK(backend->createZcBufferPool())
            << "Failed to create zero-copy buffer pool for EventBase at index "
            << i;
      }
    });
  }

  if (numHwQueues >= entries.size()) {
    // Every backend has its own HW queue, no sharing needed.
    return true;
  }

  for (size_t i = numHwQueues; i < entries.size(); ++i) {
    size_t ownerIdx = (i - numHwQueues) % numOwners;
    std::optional<IoUringZeroCopyBufferPool::ExportHandle> handle;
    entries[ownerIdx].evb->runInEventBaseThreadAndWait(
        [&handle, backend = entries[ownerIdx].backend] {
          handle.emplace(backend->exportZcBufferPool());
        });
    entries[i].evb->runInEventBaseThreadAndWait(
        [&handle, backend = entries[i].backend, i, ownerIdx] {
          CHECK(backend->importZcBufferPool(std::move(handle.value())))
              << "Failed to import buffer pool handle into EventBase at index "
              << i << " from owner at index " << ownerIdx;
        });
  }

  return true;
#endif
}

} // namespace

bool setupIoUringBufferPoolSharing(
    std::vector<folly::EventBase*>& eventBases, size_t numHwQueues) {
  return setupIoUringBufferPoolSharingImpl(
      eventBases.size(), [&](size_t i) { return eventBases[i]; }, numHwQueues);
}

} // namespace folly
