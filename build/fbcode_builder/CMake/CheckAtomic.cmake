# University of Illinois/NCSA
# Open Source License
#
# Copyright (c) 2003-2017 University of Illinois at Urbana-Champaign.
# All rights reserved.
#
# Developed by:
#
#     LLVM Team
#
#     University of Illinois at Urbana-Champaign
#
#     http://llvm.org
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal with
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is furnished to do
# so, subject to the following conditions:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimers.
#
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimers in the
#       documentation and/or other materials provided with the distribution.
#
#     * Neither the names of the LLVM Team, University of Illinois at
#       Urbana-Champaign, nor the names of its contributors may be used to
#       endorse or promote products derived from this Software without specific
#       prior written permission.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
# SOFTWARE.

include(CheckCXXSourceCompiles)

# Sometimes linking against libatomic is required for atomic ops, if
# the platform doesn't support lock-free atomics.

function(check_working_cxx_atomics varname)
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  get_directory_property(compile_options COMPILE_OPTIONS)
  set(CMAKE_REQUIRED_FLAGS ${compile_options})
  CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
int main() {
  struct Test { int val; };
  std::atomic<Test> s;
  s.is_lock_free();
}" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction(check_working_cxx_atomics)


if(NOT DEFINED PROXYGEN_COMPILER_IS_GCC_COMPATIBLE)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set(PROXYGEN_COMPILER_IS_GCC_COMPATIBLE ON)
  elseif(MSVC)
    set(PROXYGEN_COMPILER_IS_GCC_COMPATIBLE OFF)
  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(PROXYGEN_COMPILER_IS_GCC_COMPATIBLE ON)
  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Intel")
    set(PROXYGEN_COMPILER_IS_GCC_COMPATIBLE ON)
  endif()
endif()

# This isn't necessary on MSVC, so avoid command-line switch annoyance
# by only running on GCC-like hosts.
if(PROXYGEN_COMPILER_IS_GCC_COMPATIBLE)
  # First check if atomics work without the library.
  check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
  # If not, check if the library exists, and atomics work with it.
  if(NOT HAVE_CXX_ATOMICS_WITHOUT_LIB)
    check_library_exists(atomic __atomic_fetch_add_4 "" HAVE_LIBATOMIC)
    if(HAVE_LIBATOMIC)
      list(APPEND CMAKE_REQUIRED_LIBRARIES "atomic")
      check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITH_LIB)
      if (NOT HAVE_CXX_ATOMICS_WITH_LIB)
	message(FATAL_ERROR "Host compiler must support std::atomic!")
      endif()
      list(APPEND CMAKE_CXX_STANDARD_LIBRARIES -latomic)
    else()
      message(FATAL_ERROR "Host compiler appears to require libatomic, but cannot find it.")
    endif()
  endif()
endif()
