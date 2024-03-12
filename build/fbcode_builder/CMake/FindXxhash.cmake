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

#
# - Try to find Facebook xxhash library
# This will define
# XXH_FOUND
# XXH_INCLUDE_DIR
# XXH_LIBRARY
#

find_path(XXHASH_INCLUDE_DIR NAMES xxhash.h)

find_library(XXHASH_LIBRARY_DEBUG NAMES xxhashd xxhash_staticd)
find_library(XXHASH_LIBRARY_RELEASE NAMES xxhash xxhash_static)

include(SelectLibraryConfigurations)
SELECT_LIBRARY_CONFIGURATIONS(XXHASH)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    XXHASH DEFAULT_MSG
    XXHASH_LIBRARY XXHASH_INCLUDE_DIR
)

if (XXHASH_FOUND)
    message(STATUS "Found xxhash: ${XXHASH_LIBRARY}")
endif()

mark_as_advanced(XXHASH_INCLUDE_DIR XXHASH_LIBRARY)
