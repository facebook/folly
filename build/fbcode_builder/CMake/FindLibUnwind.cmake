# Copyright (c) Facebook, Inc. and its affiliates.
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

find_path(LIBUNWIND_INCLUDE_DIR NAMES libunwind.h)
mark_as_advanced(LIBUNWIND_INCLUDE_DIR)

find_library(LIBUNWIND_LIBRARY NAMES unwind)
mark_as_advanced(LIBUNWIND_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  LIBUNWIND
  REQUIRED_VARS LIBUNWIND_LIBRARY LIBUNWIND_INCLUDE_DIR)

if(LIBUNWIND_FOUND)
  set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY})
  set(LIBUNWIND_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_DIR})
endif()
