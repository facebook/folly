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

# Test definitions for folly/compression/elias_fano/test/

folly_define_tests(
  DIRECTORY compression/elias_fano/test/
      TEST compression_alias_fano_bit_vector_coding_test
        SOURCES BitVectorCodingTest.cpp
      TEST compression_alias_fano_elias_fano_test
        SOURCES EliasFanoCodingTest.cpp
)
