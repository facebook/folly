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
using namespace ::folly;

TEST(IoUringZeroCopyBufferPoolTest, GetBuf) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numPages = 32,
      .pageSize = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPool::create(params);
  io_uring_cqe cqe;
  cqe.res = 2048;
  io_uring_zcrx_cqe zcqe;
  zcqe.off = 0;
  auto buf = pool->getIoBuf(&cqe, &zcqe);
  ASSERT_EQ(buf->capacity(), 4096);
  ASSERT_EQ(buf->length(), 2048);
}

TEST(IoUringZeroCopyBufferPoolTest, DelayedDestruction) {
  IoUringZeroCopyBufferPool::Params params = {
      .ring = nullptr,
      .numPages = 32,
      .pageSize = 4096,
      .rqEntries = 8,
      .ifindex = 0,
      .queueId = 0,
  };
  auto pool = IoUringZeroCopyBufferPool::create(params);
  io_uring_cqe cqe;
  cqe.res = 2048;
  io_uring_zcrx_cqe zcqe;
  zcqe.off = 0;
  auto buf1 = pool->getIoBuf(&cqe, &zcqe);
  cqe.res = 2048;
  zcqe.off = 4096;
  auto buf2 = pool->getIoBuf(&cqe, &zcqe);
  buf1.reset();
  pool.reset();
  buf2.reset();
}
