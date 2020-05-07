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

# Provide an option to control the -std argument for the C++ compiler.
# We don't use CMAKE_CXX_STANDARD since it requires at least CMake 3.8
# to support C++17.
#
# Most users probably want to stick with the default here.  However, gnu++1z
# does change the linkage of how some symbols are emitted (e.g., constexpr
# variables defined in headers).  In case this causes problems for downstream
# libraries that aren't using gnu++1z yet, provide an option to let them still
# override this with gnu++14 if they need to.
set(
  CXX_STD "gnu++1z"
  CACHE STRING
  "The C++ standard argument to pass to the compiler.  Defaults to gnu++1z"
)
mark_as_advanced(CXX_STD)

set(CMAKE_CXX_FLAGS_COMMON "-g -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_COMMON} -O3")

# Note that CMAKE_REQUIRED_FLAGS must be a string, not a list
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=${CXX_STD}")
list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
function(apply_folly_compile_options_to_target THETARGET)
  target_compile_definitions(${THETARGET}
    PRIVATE
      _REENTRANT
      _GNU_SOURCE
  )
  target_compile_options(${THETARGET}
    PRIVATE
      -g
      -std=${CXX_STD}
      -finput-charset=UTF-8
      -fsigned-char
      -Wall
      -Wno-deprecated
      -Wno-deprecated-declarations
      -Wno-sign-compare
      -Wno-unused
      -Wunused-label
      -Wunused-result
      ${FOLLY_CXX_FLAGS}
  )
endfunction()
