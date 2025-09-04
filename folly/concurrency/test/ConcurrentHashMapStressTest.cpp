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

#include <folly/concurrency/ConcurrentHashMap.h>

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Latch.h>

template <typename T>
class ConcurrentHashMapStressTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(ConcurrentHashMapStressTest);

template <template <
    typename,
    typename,
    uint8_t,
    typename,
    typename,
    typename,
    template <typename>
    class,
    class>
          class Impl>
struct MapFactory {
  template <
      typename KeyType,
      typename ValueType,
      typename HashFn = std::hash<KeyType>,
      typename KeyEqual = std::equal_to<KeyType>,
      typename Allocator = std::allocator<uint8_t>,
      uint8_t ShardBits = 8,
      template <typename> class Atom = std::atomic,
      class Mutex = std::mutex>
  using MapT = folly::ConcurrentHashMap<
      KeyType,
      ValueType,
      HashFn,
      KeyEqual,
      Allocator,
      ShardBits,
      Atom,
      Mutex,
      Impl>;
};

#define CHM typename TypeParam::template MapT

TYPED_TEST_P(ConcurrentHashMapStressTest, StressTestReclamation) {
  // Create a map where we keep reclaiming a lot of objects that are linked to
  // one node.

  // Ensure all entries are mapped to a single segment.
  struct constant_hash {
    uint64_t operator()(unsigned long) const noexcept { return 0; }
  };
  CHM<unsigned long, unsigned long, constant_hash> map;
  static constexpr unsigned long key_prev =
      0; // A key that the test key has a link to - to guard against immediate
         // reclamation.
  static constexpr unsigned long key_test =
      1; // A key that keeps being reclaimed repeatedly.
  static constexpr unsigned long key_link_explosion =
      2; // A key that is linked to the test key.

  EXPECT_TRUE(map.insert(std::make_pair(key_prev, 0)).second);
  EXPECT_TRUE(map.insert(std::make_pair(key_test, 0)).second);
  EXPECT_TRUE(map.insert(std::make_pair(key_link_explosion, 0)).second);

  std::vector<std::thread> threads;
  // Test with (2^16)+ threads, enough to overflow a 16 bit integer.
  // It should be uncommon to have more than 2^32 concurrent accesses.
  static constexpr uint64_t num_threads = std::numeric_limits<uint16_t>::max();
  static constexpr uint64_t iters = 100;
  folly::Latch start(num_threads);
  for (uint64_t t = 0; t < num_threads; t++) {
    threads.push_back(std::thread([t, &map, &start]() {
      start.arrive_and_wait();
      static constexpr uint64_t progress_report_pct =
          (iters / 20); // Every 5% we log progress
      for (uint64_t i = 0; i < iters; i++) {
        if (t == 0 && (i % progress_report_pct) == 0) {
          // To a casual observer - to know that the test is progressing, even
          // if slowly
          LOG(INFO) << "Progress: " << (i * 100 / iters);
        }

        map.insert_or_assign(key_test, i * num_threads);
      }
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
}

REGISTER_TYPED_TEST_SUITE_P( //
    ConcurrentHashMapStressTest,
    StressTestReclamation);

using folly::detail::concurrenthashmap::bucket::BucketTable;

#if (                                                        \
    FOLLY_SSE_PREREQ(4, 2) ||                                \
    (FOLLY_AARCH64 && FOLLY_F14_CRC_INTRINSIC_AVAILABLE)) && \
    FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
using folly::detail::concurrenthashmap::simd::SIMDTable;
using MapFactoryTypes =
    ::testing::Types<MapFactory<BucketTable>, MapFactory<SIMDTable>>;
#else
using MapFactoryTypes = ::testing::Types<MapFactory<BucketTable>>;
#endif

INSTANTIATE_TYPED_TEST_SUITE_P(
    MapFactoryTypesInstantiation, ConcurrentHashMapStressTest, MapFactoryTypes);
