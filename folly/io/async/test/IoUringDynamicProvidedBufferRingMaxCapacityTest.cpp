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

#include <folly/io/async/IoUringDynamicProvidedBufferRing.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#if FOLLY_HAS_LIBURING

namespace folly {
namespace {

TEST(IoUringDynamicProvidedBufferRingMaxCapacityTest, Create) {
  io_uring ring{};
  ASSERT_EQ(0, ::io_uring_queue_init(2, &ring, 0));

  IoUringDynamicProvidedBufferRing::Options options = {
      .gid = 0,
      .bufferCount = 32768,
      .bufferSize = 32,
  };

  // A maximum-size provided-buffer ring pins 512 KiB against the per-user
  // RLIMIT_MEMLOCK budget. Other processes on the host share that budget, so
  // retry transient exhaustion while retaining coverage of the real kernel
  // registration at the supported boundary.
  constexpr auto kMaxBackoff = std::chrono::milliseconds(1000);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  auto backoff = std::chrono::milliseconds(10);
  IoUringDynamicProvidedBufferRing::UniquePtr maxRing;
  while (!maxRing) {
    try {
      maxRing = IoUringDynamicProvidedBufferRing::create(&ring, options);
    } catch (const IoUringDynamicProvidedBufferRing::OutOfMemory&) {
      if (std::chrono::steady_clock::now() + backoff > deadline) {
        throw;
      }
      std::this_thread::sleep_for(backoff);
      backoff = std::min(backoff * 2, kMaxBackoff);
    }
  }

  EXPECT_EQ(maxRing->count(), 32768);
  maxRing.reset();
  ::io_uring_queue_exit(&ring);
}

} // namespace
} // namespace folly

#endif
