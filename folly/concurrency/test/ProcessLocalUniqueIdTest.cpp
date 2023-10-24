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

#include <folly/concurrency/ProcessLocalUniqueId.h>

#include <algorithm>
#include <thread>
#include <vector>

#include <folly/Synchronized.h>
#include <folly/portability/GTest.h>

TEST(ProcessLocalUniqueIdTest, Uniqueness) {
  const size_t kNumThreads = 16;
  // Ensure each thread wraps around the counter a few times.
  constexpr static size_t kNumIdsPerThread = 1 << 18;
  const size_t kNumIds = kNumThreads * kNumIdsPerThread;

  folly::Synchronized<std::vector<uint64_t>> idsAccum;
  idsAccum.wlock()->reserve(kNumIds);

  std::vector<std::thread> threads;
  for (size_t tid = 0; tid < kNumThreads; ++tid) {
    threads.emplace_back([&idsAccum] {
      std::vector<uint64_t> threadIds;
      threadIds.reserve(kNumIdsPerThread);
      for (size_t iter = 0; iter < kNumIdsPerThread; ++iter) {
        threadIds.push_back(folly::processLocalUniqueId());
      }
      std::sort(threadIds.begin(), threadIds.end());
      auto idsAccumLocked = idsAccum.wlock();
      auto mid = idsAccumLocked->size();
      idsAccumLocked->insert(
          idsAccumLocked->end(), threadIds.begin(), threadIds.end());
      std::inplace_merge(
          idsAccumLocked->begin(),
          idsAccumLocked->begin() + mid,
          idsAccumLocked->end());
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  auto ids = idsAccum.exchange({});
  ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));
  auto end = std::unique(ids.begin(), ids.end());
  // All ids should be distinct.
  EXPECT_EQ(end - ids.begin(), kNumIds);
  // 0 should not be returned.
  EXPECT_NE(ids[0], 0);
}
