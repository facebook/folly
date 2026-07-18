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

# Test definitions for folly/compression/test/

folly_define_tests(
  DIRECTORY compression/test/
      TEST compression_compression_test SLOW SOURCES CompressionTest.cpp
      TEST compression_quotient_multiset_test SOURCES QuotientMultiSetTest.cpp
      TEST compression_select64_test SOURCES Select64Test.cpp
)
