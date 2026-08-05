# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Test definitions for folly/concurrency/test/

folly_define_tests(
  DIRECTORY concurrency/test/
      TEST concurrency_atomic_shared_ptr_test SOURCES AtomicSharedPtrTest.cpp
      TEST concurrency_cache_locality_test WINDOWS_DISABLED
        SOURCES CacheLocalityTest.cpp
      TEST concurrency_core_cached_shared_ptr_test
        SOURCES CoreCachedSharedPtrTest.cpp
      BENCHMARK concurrency_concurrent_hash_map_bench WINDOWS_DISABLED
        SOURCES ConcurrentHashMapBench.cpp
      TEST concurrency_concurrent_hash_map_stress_test SLOW WINDOWS_DISABLED
        SOURCES ConcurrentHashMapStressTest.cpp
      TEST concurrency_concurrent_hash_map_test WINDOWS_DISABLED
        SOURCES ConcurrentHashMapTest.cpp
      TEST concurrency_dynamic_bounded_queue_test WINDOWS_DISABLED
        SOURCES DynamicBoundedQueueTest.cpp
      TEST concurrency_priority_unbounded_queue_set_test
        SOURCES PriorityUnboundedQueueSetTest.cpp
      BENCHMARK concurrency_thread_cached_synchronized_bench
        SOURCES ThreadCachedSynchronizedBench.cpp
      TEST concurrency_thread_cached_synchronized_test
        SOURCES ThreadCachedSynchronizedTest.cpp
      TEST concurrency_unbounded_queue_test SOURCES UnboundedQueueTest.cpp
)
