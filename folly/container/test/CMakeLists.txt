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

# Test definitions for folly/container/test/

folly_define_tests(
  DIRECTORY container/test/
      TEST container_access_test SOURCES AccessTest.cpp
      TEST container_array_test SOURCES ArrayTest.cpp
      BENCHMARK container_bit_iterator_bench SOURCES BitIteratorBench.cpp
      TEST container_bit_iterator_test SOURCES BitIteratorTest.cpp
      TEST container_enumerate_test SOURCES EnumerateTest.cpp
      BENCHMARK container_evicting_cache_map_bench
        SOURCES EvictingCacheMapBench.cpp
      TEST container_evicting_cache_map_test SOURCES EvictingCacheMapTest.cpp
      TEST container_f14_fwd_test SOURCES F14FwdTest.cpp
      TEST container_f14_map_test SOURCES F14MapTest.cpp
      TEST container_f14_set_test SOURCES F14SetTest.cpp
      BENCHMARK container_fbvector_benchmark
        SOURCES FBVectorBenchmark.cpp
        HEADERS FBVectorBenchmarks.cpp.h
      TEST container_fbvector_test SOURCES FBVectorTest.cpp
      BENCHMARK container_foreach_benchmark SOURCES ForeachBenchmark.cpp
      TEST container_foreach_test SOURCES ForeachTest.cpp
      BENCHMARK container_hash_maps_bench SOURCES HashMapsBench.cpp
      TEST container_heap_vector_types_test SOURCES heap_vector_types_test.cpp
      TEST container_map_util_test WINDOWS_DISABLED SOURCES MapUtilTest.cpp
      TEST container_merge_test SOURCES MergeTest.cpp
      TEST container_regex_match_cache_test SOURCES RegexMatchCacheTest.cpp
      TEST container_small_vector_test WINDOWS_DISABLED
        SOURCES small_vector_test.cpp
      TEST container_sorted_vector_types_test SOURCES sorted_vector_test.cpp
      TEST container_span_test SOURCES span_test.cpp
      TEST container_std_bitset_test SOURCES StdBitsetTest.cpp
      BENCHMARK container_sparse_byte_set_benchmark
        SOURCES SparseByteSetBenchmark.cpp
      TEST container_sparse_byte_set_test SOURCES SparseByteSetTest.cpp
      TEST container_util_test SOURCES UtilTest.cpp
)
