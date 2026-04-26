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

# Test definitions for folly/functional/test/

folly_define_tests(
  DIRECTORY functional/test/
      TEST functional_apply_tuple_test WINDOWS_DISABLED
        SOURCES ApplyTupleTest.cpp
      TEST functional_invoke_test WINDOWS_DISABLED
        SOURCES InvokeTest.cpp
      TEST functional_partial_test SOURCES PartialTest.cpp
      TEST functional_protocol_test SOURCES protocol_test.cpp
      TEST functional_traits_test SOURCES traits_test.cpp
)
