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

#include <folly/io/IOBufIovecBuilder.h>

#include <folly/portability/GTest.h>

namespace {
struct TestData {
  // input
  size_t blockSize_{0};
  size_t preAllocateSize_{0};
  size_t postAllocateSize_{0};
  // output is computed
};
} // namespace

TEST(IOBufIovecBuilder, AllocateOne) {
  static constexpr std::array<TestData, 4> testData = {{
      {4096, 2000, 1000},
      {4096, 20000, 10000},
      {4096, 8192, 8192},
      {8192, 8192, 8192},
  }};
  folly::IOBufIovecBuilder::IoVecVec iovs;
  for (const auto& data : testData) {
    folly::IOBufIovecBuilder::Options options;
    options.setBlockSize(data.blockSize_);
    folly::IOBufIovecBuilder ioVecQueue(options);
    CHECK_LE(data.postAllocateSize_, data.preAllocateSize_);
    ioVecQueue.allocateBuffers(iovs, data.preAllocateSize_);
    CHECK_EQ(
        iovs.size(),
        (data.preAllocateSize_ + data.blockSize_ - 1) / data.blockSize_);
    auto ioBufs = ioVecQueue.extractIOBufChain(data.postAllocateSize_);
    CHECK_EQ(
        ioBufs->countChainElements(),
        (data.postAllocateSize_ + data.blockSize_ - 1) / data.blockSize_);
    CHECK_EQ(ioBufs->computeChainDataLength(), data.postAllocateSize_);
  }
}

TEST(IOBufIovecBuilder, AllocateMulti) {
  static constexpr std::array<TestData, 4> testData = {{
      {4096, 2000, 1000},
      {4096, 20000, 10000},
      {4096, 8192, 8192},
      {8192, 8192, 8192},
  }};
  folly::IOBufIovecBuilder::Options options;
  options.setBlockSize(4096);
  folly::IOBufIovecBuilder ioVecQueue(options);

  folly::IOBufIovecBuilder::IoVecVec iovs;

  // allocate in a loop
  for (const auto& data : testData) {
    CHECK_LE(data.postAllocateSize_, data.preAllocateSize_);
    ioVecQueue.allocateBuffers(iovs, data.preAllocateSize_);
    CHECK_GE(
        iovs.size(),
        (data.preAllocateSize_ + options.blockSize_ - 1) / options.blockSize_);
    auto ioBufs = ioVecQueue.extractIOBufChain(data.postAllocateSize_);
    CHECK_EQ(ioBufs->computeChainDataLength(), data.postAllocateSize_);
  }
}
