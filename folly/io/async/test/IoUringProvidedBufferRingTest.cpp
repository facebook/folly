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

using namespace ::testing;
using namespace ::std;
using namespace ::folly;

struct IoUringProvidedBufferRingTest : testing::Test {};

namespace folly {
class IoUringProvidedBufferRingTestHelper {
 public:
  explicit IoUringProvidedBufferRingTestHelper(IoUringProvidedBufferRing& ring)
      : ring(ring) {}

  uint32_t getRingCount() { return ring.ringCount_; }

  IoUringProvidedBufferRing& ring;
};
} // namespace folly

TEST_F(IoUringProvidedBufferRingTest, Create) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = 1000,
      .bufferSize = 4096,
      .useHugePages = true,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  EXPECT_EQ(bufRing->count(), 1000);
  EXPECT_TRUE(bufRing->available());
  EXPECT_EQ(bufRing->sizePerBuffer(), 4096);
  IoUringProvidedBufferRingTestHelper helper(*bufRing);
  EXPECT_EQ(helper.getRingCount(), 1024);
}

TEST_F(IoUringProvidedBufferRingTest, CreateNoHugepages) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = 2000,
      .bufferSize = 4096,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  EXPECT_EQ(bufRing->count(), 2000);
  EXPECT_TRUE(bufRing->available());
  EXPECT_EQ(bufRing->sizePerBuffer(), 4096);
  IoUringProvidedBufferRingTestHelper helper(*bufRing);
  EXPECT_EQ(helper.getRingCount(), 2048);
}

TEST_F(IoUringProvidedBufferRingTest, BufferMinSize) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = 10,
      .bufferSize = 8,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  EXPECT_EQ(bufRing->count(), 10);
  EXPECT_TRUE(bufRing->available());
  // constexpr size_t kMinBufferSize = 32;
  EXPECT_EQ(bufRing->sizePerBuffer(), 32);
  IoUringProvidedBufferRingTestHelper helper(*bufRing);
  EXPECT_EQ(helper.getRingCount(), 16);
}

TEST_F(IoUringProvidedBufferRingTest, DelayedDestruction) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = 1000,
      .bufferSize = 4096,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  auto buf1 = bufRing->getIoBuf(0, 1024, false);
  auto buf2 = bufRing->getIoBuf(1, 1024, false);
  buf1.reset();
  bufRing.reset();
  buf2.reset();
}

#endif
