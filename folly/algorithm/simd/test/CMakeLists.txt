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

# Test definitions for folly/algorithm/simd/test/

folly_define_tests(
  DIRECTORY algorithm/simd/test/
      TEST algorithm_simd_contains_test SOURCES ContainsTest.cpp
      TEST algorithm_simd_find_first_of_test SOURCES find_first_of_test.cpp
      TEST algorithm_simd_find_fixed_test SOURCES FindFixedTest.cpp
      TEST algorithm_simd_movemask_test SOURCES MovemaskTest.cpp
)
