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

# Test definitions for folly/test/

folly_define_tests(
  DIRECTORY test/
      TEST ahm_int_stress_test SOURCES AHMIntStressTest.cpp
      TEST arena_smartptr_test SOURCES ArenaSmartPtrTest.cpp
      BENCHMARK ascii_case_insensitive_benchmark
        SOURCES AsciiCaseInsensitiveBenchmark.cpp
      TEST ascii_check_test SOURCES AsciiCaseInsensitiveTest.cpp
      TEST atomic_hash_array_test SOURCES AtomicHashArrayTest.cpp
      TEST atomic_hash_map_test HANGING
        SOURCES AtomicHashMapTest.cpp
      TEST atomic_linked_list_test SOURCES AtomicLinkedListTest.cpp
      TEST atomic_unordered_map_test SOURCES AtomicUnorderedMapTest.cpp
      TEST base64_test SOURCES base64_test.cpp
      TEST buffered_atomic_test SOURCES BufferedAtomicTest.cpp
      TEST cancellation_token_test SOURCES CancellationTokenTest.cpp
      TEST chrono_test SOURCES ChronoTest.cpp
      TEST clock_gettime_wrappers_test SOURCES ClockGettimeWrappersTest.cpp
      TEST concurrent_bit_set_test SOURCES ConcurrentBitSetTest.cpp
      BENCHMARK concurrent_skip_list_benchmark
        SOURCES ConcurrentSkipListBenchmark.cpp
      TEST concurrent_skip_list_test SOURCES ConcurrentSkipListTest.cpp
      # Builds but fails test on constexpr_exp_floating std::exp(471.L)
      TEST constexpr_math_test WINDOWS_DISABLED
        SOURCES ConstexprMathTest.cpp
      TEST conv_test SOURCES ConvTest.cpp
      TEST cpu_id_test SOURCES CpuIdTest.cpp
      TEST demangle_test SOURCES DemangleTest.cpp
      TEST deterministic_schedule_test SOURCES DeterministicScheduleTest.cpp
      TEST discriminated_ptr_test SOURCES DiscriminatedPtrTest.cpp
      TEST endian_test SOURCES EndianTest.cpp
      TEST exception_string_test BROKEN SOURCES ExceptionStringTest.cpp
      TEST exception_test SOURCES ExceptionTest.cpp
      BENCHMARK exception_wrapper_benchmark WINDOWS_DISABLED
        SOURCES ExceptionWrapperBenchmark.cpp
      TEST exception_wrapper_test WINDOWS_DISABLED
        SOURCES ExceptionWrapperTest.cpp
      TEST expected_coroutines_test SOURCES ExpectedCoroutinesTest.cpp
      TEST expected_test WINDOWS_DISABLED
        SOURCES ExpectedTest.cpp
      BENCHMARK fbstring_benchmark WINDOWS_DISABLED
        SOURCES FBStringBenchmark.cpp
        HEADERS FBStringTestBenchmarks.cpp.h
      TEST fbstring_test WINDOWS_DISABLED SOURCES FBStringTest.cpp
      TEST file_test SOURCES FileTest.cpp
      # Open-source linux build can't handle running this.
      #TEST file_lock_test SOURCES FileLockTest.cpp
      TEST file_util_test SOURCES FileUtilTest.cpp
      BENCHMARK fingerprint_benchmark SOURCES FingerprintBenchmark.cpp
      TEST fingerprint_test SOURCES FingerprintTest.cpp
      TEST fixed_string_test SOURCES FixedStringTest.cpp
      TEST fmt_utility_test SOURCES FmtUtilityTest.cpp
      TEST format_other_test SOURCES FormatOtherTest.cpp
      BENCHMARK format_benchmark SOURCES FormatBenchmark.cpp
      TEST format_test SOURCES FormatTest.cpp
      TEST function_test BROKEN
        SOURCES FunctionTest.cpp
      BENCHMARK function_ref_benchmark SOURCES FunctionRefBenchmark.cpp
      TEST function_ref_test SOURCES FunctionRefTest.cpp
      TEST futex_test SOURCES FutexTest.cpp
      TEST glog_test SOURCES GLogTest.cpp
      TEST group_varint_test SOURCES GroupVarintTest.cpp
      TEST group_varint_test_ssse3 SOURCES GroupVarintTest.cpp
      TEST indestructible_test SOURCES IndestructibleTest.cpp
      TEST indexed_mem_pool_test BROKEN
        SOURCES IndexedMemPoolTest.cpp
      TEST iterators_test SOURCES IteratorsTest.cpp
      TEST lazy_test SOURCES LazyTest.cpp
      TEST locks_test SOURCES SpinLockTest.cpp
      TEST math_test SOURCES MathTest.cpp
      BENCHMARK memcpy_benchmark WINDOWS_DISABLED SOURCES MemcpyBenchmark.cpp
      TEST memcpy_test SOURCES MemcpyTest.cpp
      TEST memory_idler_test SOURCES MemoryIdlerTest.cpp
      TEST memory_test WINDOWS_DISABLED
        SOURCES MemoryTest.cpp
      BENCHMARK memset_benchmark WINDOWS_DISABLED SOURCES MemsetBenchmark.cpp
      TEST move_wrapper_test SOURCES MoveWrapperTest.cpp
      TEST mpmc_pipeline_test SOURCES MPMCPipelineTest.cpp
      TEST mpmc_queue_test SLOW
        SOURCES MPMCQueueTest.cpp
      TEST network_address_test HANGING
        SOURCES
          IPAddressTest.cpp
          MacAddressTest.cpp
          SocketAddressTest.cpp
      TEST optional_coroutines_test SOURCES OptionalCoroutinesTest.cpp
      TEST optional_test SOURCES OptionalTest.cpp
      TEST packed_sync_ptr_test HANGING
        SOURCES PackedSyncPtrTest.cpp
      TEST padded_test SOURCES PaddedTest.cpp
      #TEST poly_test SOURCES PolyTest.cpp
      TEST portability_test SOURCES PortabilityTest.cpp
      # Turns out this benchmark uses Linux only API for setting CPU affinity i.e. cpu_set_t.
      BENCHMARK producer_consumer_queue_benchmark APPLE_DISABLED WINDOWS_DISABLED
        SOURCES ProducerConsumerQueueBenchmark.cpp
      TEST producer_consumer_queue_test SLOW
        SOURCES ProducerConsumerQueueTest.cpp
      BENCHMARK range_find_benchmark SOURCES RangeFindBenchmark.cpp
      BENCHMARK random_benchmark SOURCES RandomBenchmark.cpp
      TEST random_test SOURCES RandomTest.cpp
      TEST range_test SOURCES RangeTest.cpp
      TEST replaceable_test WINDOWS_DISABLED SOURCES ReplaceableTest.cpp
      TEST scope_guard_test WINDOWS_DISABLED SOURCES ScopeGuardTest.cpp
      # Heavily dependent on drand and srand48
      #TEST shared_mutex_test SOURCES SharedMutexTest.cpp
      # SingletonTest requires Subprocess
      #TEST singleton_test SOURCES SingletonTest.cpp
      TEST singleton_double_registration_test BROKEN SOURCES
        SingletonDoubleRegistration.cpp
      TEST singleton_test_global SOURCES SingletonTestGlobal.cpp
      TEST singleton_thread_local_test BROKEN SOURCES
        SingletonThreadLocalTest.cpp
        SingletonThreadLocalTestOverload.cpp
      TEST spin_lock_test SOURCES SpinLockTest.cpp
      BENCHMARK string_benchmark WINDOWS_DISABLED SOURCES StringBenchmark.cpp
      TEST string_test WINDOWS_DISABLED SOURCES StringTest.cpp
      BENCHMARK string_to_float_benchmark SOURCES StringToFloatBenchmark.cpp
      BENCHMARK synchronized_benchmark WINDOWS_DISABLED
        SOURCES SynchronizedBenchmark.cpp
      TEST synchronized_test WINDOWS_DISABLED
        SOURCES SynchronizedTest.cpp
      TEST thread_cached_int_test WINDOWS_DISABLED
        SOURCES ThreadCachedIntTest.cpp
      TEST thread_local_test WINDOWS_DISABLED SOURCES ThreadLocalTest.cpp
      TEST timeout_queue_test SOURCES TimeoutQueueTest.cpp
      TEST token_bucket_test SOURCES TokenBucketTest.cpp
      TEST traits_test SOURCES TraitsTest.cpp
      TEST try_test WINDOWS_DISABLED SOURCES TryTest.cpp
      TEST unicode_test SOURCES UnicodeTest.cpp
      TEST unit_test SOURCES UnitTest.cpp
      BENCHMARK uri_benchmark SOURCES UriBenchmark.cpp
      TEST uri_test SOURCES UriTest.cpp
      TEST utf8_string_test SOURCES UTF8StringTest.cpp
      TEST utility_test SOURCES UtilityTest.cpp
      TEST varint_test SOURCES VarintTest.cpp
)

get_target_property(pic folly POSITION_INDEPENDENT_CODE)
if (pic)
  add_library(singleton_thread_local_overload
    SHARED ${FOLLY_DIR}/test/SingletonThreadLocalTestOverload.cpp)
  apply_folly_compile_options_to_target(singleton_thread_local_overload)
  set_target_properties(singleton_thread_local_overload PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/folly/test
  )
  target_link_libraries(singleton_thread_local_overload PRIVATE folly)
  folly_define_tests(
    DIRECTORY test/
      TEST singleton_thread_local_test SOURCES SingletonThreadLocalTest.cpp
  )
endif()

folly_define_tests(
  DIRECTORY test/function_benchmark/
      BENCHMARK function_benchmark
        SOURCES main.cpp benchmark_impl.cpp test_functions.cpp
)
