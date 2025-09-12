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

#include <folly/python/AsyncioExecutor.h>

FOLLY_GFLAGS_DEFINE_uint32(
    folly_asyncio_executor_drive_time_slice_ms,
    5,
    "How long AsyncioExector drive is allowed to run, "
    "ensuring that we are properly interleaving Native & Python work.");

namespace folly::python {

std::chrono::milliseconds
NotificationQueueAsyncioExecutor::getDefaultTimeSlice() {
  return std::chrono::milliseconds(
      FLAGS_folly_asyncio_executor_drive_time_slice_ms);
}

} // namespace folly::python
