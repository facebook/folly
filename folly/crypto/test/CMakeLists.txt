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

# Test definitions for folly/crypto/test/

if (${LIBSODIUM_FOUND})
  folly_define_tests(
    DIRECTORY crypto/test/
      TEST crypto_blake2xb_test SOURCES Blake2xbTest.cpp
      BENCHMARK crypto_lt_hash_benchmark SOURCES LtHashBenchmark.cpp
      TEST crypto_lt_hash_test SOURCES LtHashTest.cpp
  )
endif()
