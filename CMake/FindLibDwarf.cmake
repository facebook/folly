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

# dwarf.h is typically installed in a libdwarf/ subdirectory on Debian-style
# Linux distributions.  It is not installed in a libdwarf/ subdirectory on Mac
# systems when installed with Homebrew.  Newer homebrew installations install
# it in libdwarf-0.  Search for it in all locations.
find_path(LIBDWARF_INCLUDE_DIR NAMES dwarf.h PATH_SUFFIXES libdwarf libdwarf-0)
mark_as_advanced(LIBDWARF_INCLUDE_DIR)

find_library(LIBDWARF_LIBRARY NAMES dwarf)
mark_as_advanced(LIBDWARF_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  LIBDWARF
  REQUIRED_VARS LIBDWARF_LIBRARY LIBDWARF_INCLUDE_DIR)

if(LIBDWARF_FOUND)
  set(LIBDWARF_LIBRARIES ${LIBDWARF_LIBRARY})
  set(LIBDWARF_INCLUDE_DIRS ${LIBDWARF_INCLUDE_DIR})
endif()
