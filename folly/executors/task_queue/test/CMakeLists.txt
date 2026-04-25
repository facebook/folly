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

# Test definitions for folly/executors/task_queue/test/

folly_define_tests(
  DIRECTORY executors/task_queue/test/
      TEST executors_task_queue_priority_unbounded_blocking_queue_test
        SOURCES PriorityUnboundedBlockingQueueTest.cpp
      BENCHMARK executors_task_queue_unbounded_blocking_queue_bench
        SOURCES UnboundedBlockingQueueBench.cpp
      TEST executors_task_queue_unbounded_blocking_queue_test
        SOURCES UnboundedBlockingQueueTest.cpp

    #DIRECTORY experimental/test/
      #TEST nested_command_line_app_test SOURCES NestedCommandLineAppTest.cpp
      #TEST program_options_test SOURCES ProgramOptionsTest.cpp
      # Depends on liburcu
      #TEST read_mostly_shared_ptr_test SOURCES ReadMostlySharedPtrTest.cpp
      #TEST ref_count_test SOURCES RefCountTest.cpp

    #DIRECTORY experimental/io/test/
)
