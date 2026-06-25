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

# Test definitions for folly/synchronization/test/

folly_define_tests(
  DIRECTORY synchronization/test/
      TEST synchronization_atomic_util_test SOURCES AtomicUtilTest.cpp
      TEST synchronization_atomic_struct_test SOURCES AtomicStructTest.cpp
      BENCHMARK synchronization_baton_benchmark SOURCES BatonBenchmark.cpp
      TEST synchronization_baton_test SOURCES BatonTest.cpp
      TEST synchronization_call_once_test SOURCES CallOnceTest.cpp
      TEST synchronization_event_count_test SOURCES EventCountTest.cpp
      BENCHMARK synchronization_hazptr_bench SOURCES HazptrBench.cpp
      TEST synchronization_hazptr_test SOURCES HazptrTest.cpp
      BENCHMARK synchronization_lifo_sem_bench WINDOWS_DISABLED
        SOURCES LifoSemBench.cpp
      TEST synchronization_lifo_sem_test WINDOWS_DISABLED
        SOURCES LifoSemTests.cpp
      TEST synchronization_relaxed_atomic_test WINDOWS_DISABLED
        SOURCES RelaxedAtomicTest.cpp
      TEST synchronization_rw_spin_lock_test SOURCES RWSpinLockTest.cpp
      TEST synchronization_semaphore_test WINDOWS_DISABLED
        SOURCES SemaphoreTest.cpp
      BENCHMARK synchronization_small_locks_benchmark
        SOURCES SmallLocksBenchmark.cpp
      TEST synchronization_small_locks_test SOURCES SmallLocksTest.cpp
)
