/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <atomic>
#include <thread>
#include <vector>

#include <folly/experimental/ReadMostlySharedPtr.h>
#include <folly/portability/GTest.h>

using folly::ReadMostlyMainPtr;

class ReadMostlySharedPtrStressTest : public ::testing::Test {};

TEST_F(ReadMostlySharedPtrStressTest, ReaderWriter) {
  struct alignas(64) Data {
    std::atomic<int> lastValue{};
  };

  const static int kNumReaders = 4;
  const static int kNumIters = 100 * 1000;

  Data writerDone;
  writerDone.lastValue.store(0, std::memory_order_relaxed);

  // How the readers tell the writer to proceed.
  std::array<Data, kNumReaders> readerDone;
  for (auto& data : readerDone) {
    data.lastValue.store(0, std::memory_order_relaxed);
  }

  ReadMostlyMainPtr<int> rmmp(std::make_shared<int>(0));

  std::vector<std::thread> readers(kNumReaders);
  for (int threadId = 0; threadId < kNumReaders; ++threadId) {
    readers[threadId] = std::thread([&, threadId] {
      for (int i = 1; i < kNumIters; ++i) {
        while (writerDone.lastValue.load(std::memory_order_acquire) != i) {
          // Spin until the write has completed.
        }
        auto rmsp = rmmp.getShared();
        readerDone[threadId].lastValue.store(i, std::memory_order_release);
      }
    });
  }

  for (int i = 1; i < kNumIters; ++i) {
    auto sp = rmmp.getStdShared();
    rmmp.reset(std::make_shared<int>(i));
    writerDone.lastValue.store(i, std::memory_order_release);
    for (int threadId = 0; threadId < kNumReaders; ++threadId) {
      auto& myReaderDone = readerDone[threadId];
      while (myReaderDone.lastValue.load(std::memory_order_acquire) != i) {
        // Spin until reader has completed.
      }
    }
  }

  for (auto& thread : readers) {
    thread.join();
  }
}
