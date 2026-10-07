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

#include <folly/io/async/IoUringProvidedBufferRing.h>

#include <gtest/gtest.h>

#if FOLLY_HAS_LIBURING

namespace folly {
namespace {

TEST(IoUringProvidedBufferRingMaxCapacityTest, Create) {
  io_uring ring{};
  ASSERT_EQ(0, ::io_uring_queue_init(2, &ring, 0));

  IoUringProvidedBufferRing::Options options = {
      .gid = 0,
      .bufferCount = 32768,
      .bufferSize = 32,
  };
  auto maxRing = IoUringProvidedBufferRing::create(&ring, options);

  EXPECT_EQ(maxRing->count(), 32768);
  maxRing.reset();
  ::io_uring_queue_exit(&ring);
}

} // namespace
} // namespace folly

#endif
