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

#include <gtest/gtest.h>

#include <folly/io/async/IoUringZeroCopyBufferPool.h>

using namespace ::testing;
using namespace ::std;

namespace folly {
class IoUringZeroCopyBufferPoolTestHelper {
 public:
  static IoUringZeroCopyBufferPool::UniquePtr create(
      IoUringZeroCopyBufferPool::Params params) {
    return IoUringZeroCopyBufferPool::UniquePtr(new IoUringZeroCopyBufferPool(
        params, IoUringZeroCopyBufferPool::TestTag{}));
  }

  explicit IoUringZeroCopyBufferPoolTestHelper(IoUringZeroCopyBufferPool& pool)
      : pool(pool) {}

  uint32_t* getHead() { return pool.getHead(); }
  uint32_t getRingUsedCount() { return pool.getRingUsedCount(); }
  uint32_t getRingFreeCount() { return pool.getRingFreeCount(); }
  size_t getPendingBuffersSize() { return pool.getPendingBuffersSize(); }
  size_t getBufferSize() { return pool.getBufferSize(); }
  void* getNotifStatsPtr() { return pool.getNotifStatsPtr(); }
  uint64_t getNotifUserData() { return pool.kZcrxNotifUserData; }
  void incNoBufferCount() { pool.incNoBufferCount(); }

  void consumeRefillRingEntries(uint32_t numEntries) {
    *getHead() += numEntries;
  }
  IoUringZeroCopyBufferPool& pool;
};
} // namespace folly

using namespace ::folly;

// TODO: T264228475: remove this once liburing/uapi headers are updated.
struct TestZcrxNotifStats {
  uint64_t copy_count;
  uint64_t copy_bytes;
};

TEST(IoUringZeroCopyBufferPoolTest, GetBuf) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 32,
      .bufferSizeHint = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  io_uring_cqe cqe{};
  cqe.res = 2048;
  io_uring_zcrx_cqe zcqe{};
  zcqe.off = 0;
  auto buf = pool->getIoBuf(&cqe, &zcqe);
  ASSERT_EQ(buf->length(), 2048);
  EXPECT_EQ(buf->capacity() >= buf->length(), true);
}

TEST(IoUringZeroCopyBufferPoolTest, DelayedDestruction) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 32,
      .bufferSizeHint = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  io_uring_cqe cqe{};
  cqe.res = 2048;
  io_uring_zcrx_cqe zcqe{};
  zcqe.off = 0;
  auto buf1 = pool->getIoBuf(&cqe, &zcqe);
  cqe.res = 2048;
  zcqe.off = 4096;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);
  buf1.reset();
  pool.reset();
  buf2.reset();
}

TEST(IoUringZeroCopyBufferPoolTest, RefillTest) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 2,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);
  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getRingFreeCount(), 2);

  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};

  cqe.res = 2048;
  zcqe.off = 0;
  auto buf1 = pool->getIoBuf(&cqe, &zcqe);

  zcqe.off += 4096;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);

  zcqe.off += 4096;
  auto buf3 = pool->getIoBuf(&cqe, &zcqe);

  buf1.reset();
  buf2.reset();
  buf3.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 1);

  helper.consumeRefillRingEntries(2);
  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getRingFreeCount(), 2);
  EXPECT_EQ(helper.getPendingBuffersSize(), 1);

  zcqe.off += 4096;
  auto buf4 = pool->getIoBuf(&cqe, &zcqe);
  buf4.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);
}

TEST(IoUringZeroCopyBufferPoolTest, RefillWithLargePendingQueue) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 200,
      .bufferSizeHint = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);

  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getRingFreeCount(), 8);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);

  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};
  cqe.res = 2048;
  zcqe.off = 0;

  for (int i = 0; i < 150; i++) {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
  }

  EXPECT_EQ(helper.getRingUsedCount(), 8);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 142);

  helper.consumeRefillRingEntries(8);

  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getRingFreeCount(), 8);
  EXPECT_EQ(helper.getPendingBuffersSize(), 142);

  {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
  }

  EXPECT_EQ(helper.getRingUsedCount(), 8);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 135);
  helper.consumeRefillRingEntries(8);

  for (int i = 0; i < 10; i++) {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
  }

  EXPECT_EQ(helper.getRingUsedCount(), 8);
  EXPECT_EQ(helper.getPendingBuffersSize(), 137);

  helper.consumeRefillRingEntries(8);

  while (helper.getPendingBuffersSize() > 0) {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
    if (helper.getRingUsedCount() == 8) {
      helper.consumeRefillRingEntries(8);
    }
  }
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);

  pool.reset();
}

TEST(IoUringZeroCopyBufferPoolTest, SharedBufferGetIoBuf) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 32,
      .bufferSizeHint = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);

  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};

  // Dispense 3 IOBufs: two sharing buffers_[0], one on buffers_[1].
  cqe.res = 1500;
  zcqe.off = 0;
  auto buf1 = pool->getIoBuf(&cqe, &zcqe);

  cqe.res = 800;
  zcqe.off = 1600;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);

  EXPECT_EQ(buf1->length(), 1500);
  EXPECT_EQ(buf2->length(), 800);
  EXPECT_EQ(buf2->data() - buf1->data(), 1600);

  cqe.res = 2048;
  zcqe.off = 4096;
  auto buf3 = pool->getIoBuf(&cqe, &zcqe);
  EXPECT_EQ(buf3->length(), 2048);

  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);

  buf1.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 1);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);

  buf2.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);

  buf3.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 3);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);
}

TEST(IoUringZeroCopyBufferPoolTest, SharedBufferRefillWithPending) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 2,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);

  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};

  // Dispense 3 IOBufs: two sharing buffers_[0], one on buffers_[1].
  // Each uses a distinct length to avoid false positives.
  // This results in 3 returnBuffer() calls when freed.
  cqe.res = 1500;
  zcqe.off = 0;
  auto buf1 = pool->getIoBuf(&cqe, &zcqe);
  cqe.res = 2048;
  zcqe.off = 4096;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);
  cqe.res = 800;
  zcqe.off = 1600;
  auto buf3 = pool->getIoBuf(&cqe, &zcqe);

  buf1.reset();
  buf2.reset();
  buf3.reset();
  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 1);

  helper.consumeRefillRingEntries(2);
  EXPECT_EQ(helper.getRingFreeCount(), 2);

  zcqe.off = 2 * 4096;
  auto buf4 = pool->getIoBuf(&cqe, &zcqe);
  buf4.reset();

  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getPendingBuffersSize(), 0);
}

TEST(IoUringZeroCopyBufferPoolTest, CqeIsNotif) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 4,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);

  io_uring_cqe cqe{};

  // Notification sentinel value.
  cqe.user_data = helper.getNotifUserData();
  EXPECT_TRUE(pool->cqeIsNotif(&cqe));

  // Regular CQE with zero user_data.
  cqe.user_data = 0;
  EXPECT_FALSE(pool->cqeIsNotif(&cqe));

  // Regular CQE with arbitrary user_data.
  cqe.user_data = 42;
  EXPECT_FALSE(pool->cqeIsNotif(&cqe));
}

TEST(IoUringZeroCopyBufferPoolTest, NotifStats) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 4,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);
  auto* stats = static_cast<TestZcrxNotifStats*>(helper.getNotifStatsPtr());
  ASSERT_NE(stats, nullptr);

  EXPECT_EQ(pool->getAndResetCopyFallbackCount(), 0);
  EXPECT_EQ(pool->getAndResetCopyFallbackBytes(), 0);

  __atomic_store_n(&stats->copy_count, 5, __ATOMIC_RELAXED);
  __atomic_store_n(&stats->copy_bytes, 65536, __ATOMIC_RELAXED);
  EXPECT_EQ(pool->getAndResetCopyFallbackCount(), 5);
  EXPECT_EQ(pool->getAndResetCopyFallbackBytes(), 65536);

  EXPECT_EQ(pool->getAndResetCopyFallbackCount(), 0);
  EXPECT_EQ(pool->getAndResetCopyFallbackBytes(), 0);

  __atomic_add_fetch(&stats->copy_count, 20, __ATOMIC_RELAXED);
  __atomic_add_fetch(&stats->copy_bytes, 131072, __ATOMIC_RELAXED);
  EXPECT_EQ(pool->getAndResetCopyFallbackCount(), 20);
  EXPECT_EQ(pool->getAndResetCopyFallbackBytes(), 131072);
}

TEST(IoUringZeroCopyBufferPoolTest, NoBufferCount) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 4,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);

  // Initially zero.
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 0);

  // Single increment.
  helper.incNoBufferCount();
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 1);

  // Reset back to zero after read.
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 0);

  // Multiple increments accumulate correctly.
  helper.incNoBufferCount();
  helper.incNoBufferCount();
  helper.incNoBufferCount();
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 3);

  // Resets again after read.
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 0);

  // No-buffer counting works correctly interleaved with buffer ops.
  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};
  cqe.res = 2048;
  zcqe.off = 0;

  auto buf1 = pool->getIoBuf(&cqe, &zcqe);
  helper.incNoBufferCount();
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 1);

  buf1.reset();
  zcqe.off = 4096;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);
  helper.incNoBufferCount();
  helper.incNoBufferCount();
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 2);

  // Buffer operations don't affect the counter after reset.
  buf2.reset();
  EXPECT_EQ(pool->getAndResetNoBufferCount(), 0);
}

TEST(IoUringZeroCopyBufferPoolTest, BufferSizeHintFallback) {
  size_t pageSize = sysconf(_SC_PAGESIZE);

  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 0,
      .rqEntries = 4,
      .ifindex = 0,
      .queueId = 0,
  };

  // Hint too small: falls back to page size.
  params.bufferSizeHint = 512;
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);
  EXPECT_EQ(helper.getBufferSize(), pageSize);

  // Non-power-of-two hint: falls back to page size.
  params.bufferSizeHint = 6000;
  pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper2(*pool);
  EXPECT_EQ(helper2.getBufferSize(), pageSize);

  // Exact page size hint: accepted as-is.
  params.bufferSizeHint = pageSize;
  pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper3(*pool);
  EXPECT_EQ(helper3.getBufferSize(), pageSize);
}

TEST(IoUringZeroCopyBufferPoolTest, RefillMoreThanCapacity) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numBuffers = 8,
      .bufferSizeHint = 4096,
      .rqEntries = 2,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPoolTestHelper::create(params);
  IoUringZeroCopyBufferPoolTestHelper helper(*pool);
  EXPECT_EQ(helper.getRingUsedCount(), 0);
  EXPECT_EQ(helper.getRingFreeCount(), 2);

  io_uring_cqe cqe{};
  io_uring_zcrx_cqe zcqe{};

  cqe.res = 2048;
  zcqe.off = 0;
  for (int i = 0; i < 8; i++) {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
  }

  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 6);

  helper.consumeRefillRingEntries(2);
  zcqe.off = 0;
  for (int i = 0; i < 2; i++) {
    auto buf = pool->getIoBuf(&cqe, &zcqe);
    zcqe.off += 4096;
  }
  EXPECT_EQ(helper.getRingUsedCount(), 2);
  EXPECT_EQ(helper.getRingFreeCount(), 0);
  EXPECT_EQ(helper.getPendingBuffersSize(), 6);
  pool.reset();
}
