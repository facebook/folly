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

# Test definitions for folly/executors/test/

folly_define_tests(
  DIRECTORY executors/test/
      TEST executors_async_helpers_test SOURCES AsyncTest.cpp
      TEST executors_codel_test WINDOWS_DISABLED SOURCES CodelTest.cpp
      BENCHMARK executors_edf_thread_pool_executor_benchmark
        SOURCES EDFThreadPoolExecutorBenchmark.cpp
      TEST executors_executor_test SOURCES ExecutorTest.cpp
      TEST executors_fiber_io_executor_test SOURCES FiberIOExecutorTest.cpp
      # FunctionSchedulerTest has a lot of timing-dependent checks,
      # and tends to fail on heavily loaded systems.
      TEST executors_function_scheduler_test BROKEN
        SOURCES FunctionSchedulerTest.cpp
      TEST executors_global_executor_test SOURCES GlobalExecutorTest.cpp
      TEST executors_serial_executor_test SOURCES SerialExecutorTest.cpp
      # Fails in ThreadPoolExecutorTest.RequestContext:719 data2 != nullptr
      TEST executors_thread_pool_executor_test BROKEN WINDOWS_DISABLED
        SOURCES ThreadPoolExecutorTest.cpp
      TEST executors_threaded_executor_test SOURCES ThreadedExecutorTest.cpp
      TEST executors_timed_drivable_executor_test
        SOURCES TimedDrivableExecutorTest.cpp
)
