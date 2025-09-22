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

#include <chrono>
#include <cstdint>
#include <thread>

#include <folly/CancellationToken.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Task.h>
#include <folly/futures/Future.h>
#include <folly/portability/GFlags.h>
#include <folly/python/AsyncioExecutor.h>
#include <folly/python/executor.h>

namespace folly {
namespace python {
namespace test {

inline folly::coro::Task<uint64_t> coro_getValueX5(uint64_t val) {
  if (val == 0) {
    throw std::invalid_argument("0 is not allowed");
  }
  co_return val * 5;
}

inline folly::coro::Task<uint64_t> coro_returnFiveAfterCancelled() {
  folly::coro::Baton baton;
  const folly::CancellationToken& ct =
      co_await folly::coro::co_current_cancellation_token;
  folly::CancellationCallback cb{ct, [&] { baton.post(); }};
  co_await baton;
  co_return 5;
}

inline coro::Task<uint64_t> coro_sleepThenEcho(
    uint32_t sleepMs, uint64_t echoVal) {
  co_await folly::futures::sleep(std::chrono::milliseconds{sleepMs});
  co_return echoVal;
}

inline NotificationQueueAsyncioExecutor* getNotificationQueueAsyncioExecutor() {
  auto* executor = getExecutor();
  return dynamic_cast<NotificationQueueAsyncioExecutor*>(executor);
}

inline coro::Task<uint64_t> coro_blockingTask(
    uint32_t blockMs, uint64_t echoVal) {
  co_await coro::co_reschedule_on_current_executor;
  // Block the thread with sleep to simulate blocking work
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(std::chrono::milliseconds{blockMs});
  co_return echoVal;
}

inline void setAsyncioExecutorDriveTimeSliceMs(uint32_t timeSliceMs) {
  FLAGS_folly_asyncio_executor_drive_time_slice_ms = timeSliceMs;
}

} // namespace test
} // namespace python
} // namespace folly
