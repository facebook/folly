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

# Test definitions for folly/gen/test/

folly_define_tests(
  DIRECTORY gen/test/
      # MSVC bug can't resolve initializer_list constructor properly
      TEST gen_base_test WINDOWS_DISABLED SOURCES BaseTest.cpp
      TEST gen_combine_test SOURCES CombineTest.cpp
      BENCHMARK gen_parallel_map_benchmark SOURCES ParallelMapBenchmark.cpp
      TEST gen_parallel_map_test SOURCES ParallelMapTest.cpp
      BENCHMARK gen_parallel_benchmark SOURCES ParallelBenchmark.cpp
      TEST gen_parallel_test SOURCES ParallelTest.cpp
)
