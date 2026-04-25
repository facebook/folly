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

# Test definitions for folly/system/test/

folly_define_tests(
  DIRECTORY system/test/
      TEST system_at_fork_test WINDOWS_DISABLED SOURCES AtForkTest.cpp
      TEST system_memory_mapping_test SOURCES MemoryMappingTest.cpp
      TEST system_shell_test SOURCES ShellTest.cpp
      #TEST system_subprocess_test SOURCES SubprocessTest.cpp
      TEST system_thread_id_test SOURCES ThreadIdTest.cpp
      TEST system_thread_name_test WINDOWS_DISABLED
        SOURCES ThreadNameTest.cpp
)
