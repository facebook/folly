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

# Test definitions for folly/hash/test/

folly_define_tests(
  DIRECTORY hash/test/
      BENCHMARK hash_checksum_benchmark SOURCES ChecksumBenchmark.cpp
      # builds, but tests fail on MSVC cmake build
      TEST hash_checksum_test WINDOWS_DISABLED
        SOURCES ChecksumTest.cpp
      TEST hash_farm_hash_test SOURCES FarmHashTest.cpp
      BENCHMARK hash_hash_benchmark WINDOWS_DISABLED
        SOURCES HashBenchmark.cpp
      TEST hash_hash_test WINDOWS_DISABLED
        SOURCES HashTest.cpp
      TEST hash_spooky_hash_v1_test SOURCES SpookyHashV1Test.cpp
      TEST hash_spooky_hash_v2_test SOURCES SpookyHashV2Test.cpp
      TEST hash_traits_test SOURCES traits_test.cpp
      TEST hash_unique_hash_key_test SOURCES UniqueHashKeyTest.cpp
)
