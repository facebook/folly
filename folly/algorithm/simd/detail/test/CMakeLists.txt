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

# Test definitions for folly/algorithm/simd/detail/test/

folly_define_tests(
  DIRECTORY algorithm/simd/detail/test/
      TEST algorithm_simd_detail_simd_any_of_test SOURCES SimdAnyOfTest.cpp
      TEST algorithm_simd_detail_simd_for_each_test SOURCES SimdForEachTest.cpp
      TEST algorithm_simd_detail_simd_traits_test SOURCES TraitsTest.cpp
      TEST algorithm_simd_detail_unroll_utils_test SOURCES UnrollUtilsTest.cpp
)
