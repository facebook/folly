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

# Test definitions for folly/json/test/

folly_define_tests(
  DIRECTORY json/test/
      TEST json_dynamic_converter_test SOURCES DynamicConverterTest.cpp
      TEST json_dynamic_other_test SOURCES DynamicOtherTest.cpp
      TEST json_dynamic_parser_test SOURCES DynamicParserTest.cpp
      TEST json_dynamic_test SOURCES DynamicTest.cpp
      # MSVC Preprocessor stringizing raw string literals bug
      TEST json_json_test WINDOWS_DISABLED SOURCES JsonTest.cpp
      BENCHMARK json_json_benchmark SOURCES JsonBenchmark.cpp
      TEST json_json_other_test SOURCES JsonOtherTest.cpp
      TEST json_json_patch_test SOURCES json_patch_test.cpp
      TEST json_json_pointer_test SOURCES json_pointer_test.cpp
      TEST json_json_schema_test SOURCES JSONSchemaTest.cpp
)
