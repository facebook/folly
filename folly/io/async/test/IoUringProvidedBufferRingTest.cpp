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

#include <atomic>
#include <thread>
#include <vector>

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

  uint32_t ringCount() { return ring.ringCount_; }
  uint32_t returnedBuffers() { return ring.returnedBuffers_; }

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
  EXPECT_EQ(helper.ringCount(), 1024);
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
  EXPECT_EQ(helper.ringCount(), 2048);
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
  EXPECT_EQ(helper.ringCount(), 16);
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

TEST_F(IoUringProvidedBufferRingTest, ConcurrentDecBufferState) {
  constexpr size_t kBufsPerThread = 1024;
  constexpr int kNumThreads = 16;
  constexpr uint32_t kBufferCount = kBufsPerThread * kNumThreads;

  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = kBufferCount,
      .bufferSize = 64,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);

  // Acquire all buffers
  std::vector<std::unique_ptr<IOBuf>> bufs;
  bufs.reserve(kBufferCount);
  for (uint32_t i = 0; i < kBufferCount; i++) {
    bufs.push_back(bufRing->getIoBuf(i, 32, false));
  }

  // Distribute buffers across threads
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < kNumThreads; t++) {
    size_t start = t * kBufsPerThread;
    std::vector<std::unique_ptr<IOBuf>> threadBufs;
    threadBufs.reserve(kBufsPerThread);
    for (size_t i = start; i < start + kBufsPerThread; i++) {
      threadBufs.push_back(std::move(bufs[i]));
    }

    threads.emplace_back([&go, threadBufs = std::move(threadBufs)]() mutable {
      while (!go.load(std::memory_order_acquire)) {
      }
      // Each reset triggers free_fn -> decBufferState
      for (auto& buf : threadBufs) {
        buf.reset();
      }
    });
  }

  go.store(true, std::memory_order_release);
  for (auto& t : threads) {
    t.join();
  }

  IoUringProvidedBufferRingTestHelper helper(*bufRing);
  EXPECT_EQ(helper.returnedBuffers(), kBufferCount);
}

TEST_F(
    IoUringProvidedBufferRingTest, IncrementalPartiallyConsumedSingleBuffer) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .bufferCount = 4,
      .bufferSize = 64,
      .useHugePages = false,
      .useIncrementalBuffers = true,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);

  auto first = bufRing->getIoBuf(0, 30, true);
  EXPECT_EQ(first->length(), 30);

  auto second = bufRing->getIoBuf(0, 40, false);
  EXPECT_TRUE(second->isChained());
  EXPECT_EQ(second->computeChainDataLength(), 40);
  EXPECT_EQ(second->length(), 34);
  EXPECT_EQ(second->next()->length(), 6);
}

#endif
