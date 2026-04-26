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

# Test definitions for folly/portability/test/

folly_define_tests(
  DIRECTORY portability/test/
      TEST portability_constexpr_test SOURCES ConstexprTest.cpp
      TEST portability_filesystem_test SOURCES FilesystemTest.cpp
      TEST portability_libgen_test SOURCES LibgenTest.cpp
      TEST portability_openssl_portability_test
        SOURCES OpenSSLPortabilityTest.cpp
      TEST portability_pthread_test SOURCES PThreadTest.cpp
      TEST portability_time_test WINDOWS_DISABLED
        SOURCES TimeTest.cpp
)
