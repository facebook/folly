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

# Test definitions for folly/lang/test/

folly_define_tests(
  DIRECTORY lang/test/
      TEST lang_align_test SOURCES AlignTest.cpp
      TEST lang_aligned_test SOURCES AlignedTest.cpp
      TEST lang_badge_test SOURCES BadgeTest.cpp
      TEST lang_bits_class_test SOURCES BitsClassTest.cpp
      TEST lang_bits_test SOURCES BitsTest.cpp
      TEST lang_c_string_test SOURCES CStringTest.cpp
      TEST lang_cast_test SOURCES CastTest.cpp
      TEST lang_checked_math_test SOURCES CheckedMathTest.cpp
      TEST lang_exception_test SOURCES ExceptionTest.cpp
      TEST lang_extern_test SOURCES ExternTest.cpp
      TEST lang_ordering_test SOURCES OrderingTest.cpp
      TEST lang_pretty_test SOURCES PrettyTest.cpp
      TEST lang_propagate_const_test SOURCES PropagateConstTest.cpp
      TEST lang_r_value_reference_wrapper_test WINDOWS_DISABLED
        SOURCES RValueReferenceWrapperTest.cpp
      TEST lang_safe_assert_test SOURCES SafeAssertTest.cpp
      TEST lang_switch_test SOURCES SwitchTest.cpp
      BENCHMARK lang_to_ascii_benchmark SOURCES ToAsciiBench.cpp
      TEST lang_to_ascii_test SOURCES ToAsciiTest.cpp
      TEST lang_type_info_test SOURCES TypeInfoTest.cpp
)
