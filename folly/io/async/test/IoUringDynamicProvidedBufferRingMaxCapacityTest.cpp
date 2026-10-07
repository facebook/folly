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

#include <gtest/gtest.h>

#if FOLLY_HAS_LIBURING

namespace folly {
namespace {

TEST(
    IoUringDynamicProvidedBufferRingMaxCapacityTest,
    AcceptsMaximumBufferCount) {
  io_uring ring{};
  ring.ring_fd = -1;

  IoUringDynamicProvidedBufferRing::Options options = {
      .gid = 0,
      .bufferCount = 32768,
      .bufferSize = 32,
  };

  // Reaching the registration-specific error proves that validation accepted
  // the maximum without consuming the host's shared memlock budget.
  EXPECT_THROW(
      IoUringDynamicProvidedBufferRing::create(&ring, options),
      IoUringDynamicProvidedBufferRing::LibUringCallError);
}

} // namespace
} // namespace folly

#endif
