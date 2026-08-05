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

# Test definitions for folly/memory/test/

folly_define_tests(
  DIRECTORY memory/test/
      TEST memory_arena_test WINDOWS_DISABLED SOURCES ArenaTest.cpp
      TEST memory_reentrant_allocator_test WINDOWS_DISABLED
        SOURCES ReentrantAllocatorTest.cpp
      TEST memory_shared_from_this_ptr_test
        SOURCES shared_from_this_ptr_test.cpp
      TEST memory_thread_cached_arena_test WINDOWS_DISABLED
        SOURCES ThreadCachedArenaTest.cpp
      TEST memory_mallctl_helper_test SOURCES MallctlHelperTest.cpp
      TEST memory_uninitialized_memory_hacks_test
        SOURCES UninitializedMemoryHacksTest.cpp
)
