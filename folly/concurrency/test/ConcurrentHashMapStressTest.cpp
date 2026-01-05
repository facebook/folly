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

#include <folly/portability/SysMman.h>

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
    template <typename> class,
    class> class Impl>
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
  using Map = CHM<unsigned long, unsigned long, constant_hash>;
  static constexpr unsigned long key_prev =
      0; // A key that the test key has a link to - to guard against immediate
         // reclamation.
  static constexpr unsigned long key_test =
      1; // A key that keeps being reclaimed repeatedly.
  static constexpr unsigned long key_link_explosion =
      2; // A key that is linked to the test key.

  Map map;
  EXPECT_TRUE(map.insert(std::make_pair(key_prev, 0)).second);
  EXPECT_TRUE(map.insert(std::make_pair(key_test, 0)).second);
  EXPECT_TRUE(map.insert(std::make_pair(key_link_explosion, 0)).second);

  struct alignas(32) ThreadInfo {
    pthread_t id;
    uint64_t idx;
    Map* map;
    folly::Latch* start;
  };

  // Test with (2^16)+ threads, enough to overflow a 16 bit integer.
  // It should be uncommon to have more than 2^32 concurrent accesses.
  static constexpr uint64_t num_threads = 1u << 16; // 2^16
  static constexpr uint64_t iters = 100;
  // Separately allocating the thread args would be costly. This cost can be
  // optimized by allocating them all together.
  std::vector<ThreadInfo> threads;
  threads.reserve(num_threads);
  folly::Latch start(num_threads);

  // Separately mapping each thread stack would be costly. This cost can be
  // optimized by mapping all thread stacks together at once. Separately
  // faulting in each top page of each thread stack is costly. This cost can be
  // optimized by prefaulting the entire mapping. There will still be a TLB miss
  // in each thread but perhaps that would be tolerable.
  constexpr size_t stack_size = 1u << 16; // 64KiB
#if !defined(_WIN32)
  const auto mm_len = stack_size * num_threads;
  const auto mm_prot = PROT_READ | PROT_WRITE;
  const auto mm_flags =
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_POPULATE;
  const auto stack_array = ::mmap(nullptr, mm_len, mm_prot, mm_flags, -1, 0);
  ASSERT_NE(stack_array, MAP_FAILED);
  SCOPE_EXIT {
    ::munmap(stack_array, stack_size * num_threads);
  };
#endif

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack_size);
#if !defined(_WIN32)
  // Since the thread stacks are all allocated together, there are no guard
  // pages.
  pthread_attr_setguardsize(&attr, 0);
#endif

  auto thfunc = +[](void* arg) -> void* {
    auto info = *static_cast<ThreadInfo*>(arg);
    auto t = info.idx;
    auto& map = *info.map;
    auto& start = *info.start;

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
    return nullptr;
  };

  for (uint64_t t = 0; t < num_threads; t++) {
#if !defined(_WIN32)
    void* stack_base = (char*)stack_array + (t * stack_size);
    pthread_attr_setstack(&attr, stack_base, stack_size);
#endif
    auto& tharg = threads.emplace_back(
        ThreadInfo{.id = {}, .idx = t, .map = &map, .start = &start});
    auto ret = pthread_create(&tharg.id, &attr, thfunc, &tharg);
    PCHECK(ret == 0);
  }
  for (auto& t : threads) {
    void* retval = nullptr;
    auto ret = pthread_join(t.id, &retval);
    PCHECK(ret == 0);
    DCHECK(retval == nullptr);
  }

  pthread_attr_destroy(&attr);
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
