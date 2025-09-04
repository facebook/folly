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

int get_shift(int x) {
  int shift = findLastSet(x) - 1;
  if (x != (1 << shift)) {
    shift++;
  }
  return shift;
}

struct IoUringProvidedBufferRingTest : testing::Test {};

TEST_F(IoUringProvidedBufferRingTest, Create) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  int sizeShift = std::max<int>(get_shift(4096), 5);
  int ringShift = std::max<int>(get_shift(1000), 1);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .count = 1000,
      .bufferShift = sizeShift,
      .ringSizeShift = ringShift,
      .useHugePages = true,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  EXPECT_EQ(bufRing->count(), 1000);
}

TEST_F(IoUringProvidedBufferRingTest, CreateNoHugepages) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  int sizeShift = std::max<int>(get_shift(4096), 5);
  int ringShift = std::max<int>(get_shift(1000), 1);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .count = 1000,
      .bufferShift = sizeShift,
      .ringSizeShift = ringShift,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  EXPECT_EQ(bufRing->count(), 1000);
}

TEST_F(IoUringProvidedBufferRingTest, DelayedDestruction) {
  io_uring ring{};
  io_uring_queue_init(512, &ring, 0);
  int sizeShift = std::max<int>(get_shift(4096), 5);
  int ringShift = std::max<int>(get_shift(1000), 1);
  IoUringProvidedBufferRing::Options options = {
      .gid = 1,
      .count = 1000,
      .bufferShift = sizeShift,
      .ringSizeShift = ringShift,
      .useHugePages = false,
  };
  auto bufRing = IoUringProvidedBufferRing::create(&ring, options);
  auto buf1 = bufRing->getIoBuf(0, 1024);
  auto buf2 = bufRing->getIoBuf(1, 1024);
  buf1.reset();
  bufRing.reset();
  buf2.reset();
}

#endif
