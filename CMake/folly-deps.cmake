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

include(CheckCXXSourceCompiles)
include(CheckIncludeFileCXX)
include(CheckFunctionExists)

set(
  BOOST_LINK_STATIC "auto"
  CACHE STRING
  "Whether to link against boost statically or dynamically."
)
if("${BOOST_LINK_STATIC}" STREQUAL "auto")
  # Default to linking boost statically on Windows with MSVC
  if(MSVC)
    set(FOLLY_BOOST_LINK_STATIC ON)
  else()
    set(FOLLY_BOOST_LINK_STATIC OFF)
  endif()
else()
  set(FOLLY_BOOST_LINK_STATIC "${BOOST_LINK_STATIC}")
endif()
set(Boost_USE_STATIC_LIBS "${FOLLY_BOOST_LINK_STATIC}")

find_package(Boost 1.51.0 MODULE
  COMPONENTS
    context
    filesystem
    program_options
    regex
    system
    thread
  REQUIRED
)
list(APPEND FOLLY_LINK_LIBRARIES ${Boost_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIRS})

find_package(DoubleConversion MODULE REQUIRED)
list(APPEND FOLLY_LINK_LIBRARIES ${DOUBLE_CONVERSION_LIBRARY})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${DOUBLE_CONVERSION_INCLUDE_DIR})

find_package(Gflags MODULE)
set(FOLLY_HAVE_LIBGFLAGS ${LIBGFLAGS_FOUND})
if(LIBGFLAGS_FOUND)
  list(APPEND FOLLY_LINK_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBGFLAGS_INCLUDE_DIR})
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBGFLAGS_INCLUDE_DIR})
endif()

find_package(Glog MODULE)
set(FOLLY_HAVE_LIBGLOG ${GLOG_FOUND})
list(APPEND FOLLY_LINK_LIBRARIES ${GLOG_LIBRARY})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${GLOG_INCLUDE_DIR})

find_package(LibEvent MODULE REQUIRED)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBEVENT_LIB})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBEVENT_INCLUDE_DIR})

find_package(OpenSSL MODULE REQUIRED)
list(APPEND FOLLY_LINK_LIBRARIES ${OPENSSL_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${OPENSSL_LIBRARIES})
list(APPEND CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
check_function_exists(ASN1_TIME_diff FOLLY_HAVE_OPENSSL_ASN1_TIME_DIFF)

find_package(ZLIB MODULE)
set(FOLLY_HAVE_LIBZ ${ZLIB_FOUND})
if (ZLIB_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${ZLIB_INCLUDE_DIRS})
  list(APPEND FOLLY_LINK_LIBRARIES ${ZLIB_LIBRARIES})
endif()

find_package(BZip2 MODULE)
set(FOLLY_HAVE_LIBBZ2 ${BZIP2_FOUND})
if (BZIP2_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${BZIP2_INCLUDE_DIRS})
  list(APPEND FOLLY_LINK_LIBRARIES ${BZIP2_LIBRARIES})
endif()

find_package(LibLZMA MODULE)
set(FOLLY_HAVE_LIBLZMA ${LIBLZMA_FOUND})
if (LIBLZMA_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBLZMA_INCLUDE_DIRS})
  list(APPEND FOLLY_LINK_LIBRARIES ${LIBLZMA_LIBRARIES})
endif()

find_package(LZ4 MODULE)
set(FOLLY_HAVE_LIBLZ4 ${LZ4_FOUND})
if (LZ4_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LZ4_INCLUDE_DIR})
  list(APPEND FOLLY_LINK_LIBRARIES ${LZ4_LIBRARY})
endif()

find_package(Zstd MODULE)
set(FOLLY_HAVE_LIBZSTD ${ZSTD_FOUND})
if(ZSTD_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${ZSTD_INCLUDE_DIR})
  list(APPEND FOLLY_LINK_LIBRARIES ${ZSTD_LIBRARY})
endif()

find_package(Snappy MODULE)
set(FOLLY_HAVE_LIBSNAPPY ${SNAPPY_FOUND})
if (SNAPPY_FOUND)
  list(APPEND FOLLY_INCLUDE_DIRECTORIES ${SNAPPY_INCLUDE_DIR})
  list(APPEND FOLLY_LINK_LIBRARIES ${SNAPPY_LIBRARY})
endif()

find_package(LibDwarf)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBDWARF_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBDWARF_INCLUDE_DIRS})

find_package(Libiberty)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBIBERTY_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBIBERTY_INCLUDE_DIRS})

find_package(LibAIO)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBAIO_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBAIO_INCLUDE_DIRS})

find_package(LibUring)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBURING_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBURING_INCLUDE_DIRS})

find_package(Libsodium)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBSODIUM_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBSODIUM_INCLUDE_DIRS})

list(APPEND FOLLY_LINK_LIBRARIES ${CMAKE_DL_LIBS})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${CMAKE_DL_LIBS})

if (PYTHON_EXTENSIONS)
  find_package(PythonInterp 3.6 REQUIRED)
  find_package(Cython 0.26 REQUIRED)
endif ()

find_package(LibUnwind)
list(APPEND FOLLY_LINK_LIBRARIES ${LIBUNWIND_LIBRARIES})
list(APPEND FOLLY_INCLUDE_DIRECTORIES ${LIBUNWIND_INCLUDE_DIRS})

set(FOLLY_USE_SYMBOLIZER OFF)
CHECK_INCLUDE_FILE_CXX(elf.h FOLLY_HAVE_ELF)
if (LIBUNWIND_FOUND)
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${UNWIND_LIBRARIES})
endif()
find_package(Backtrace)
set(FOLLY_HAVE_BACKTRACE ${Backtrace_FOUND})
set(FOLLY_HAVE_DWARF ${LIBDWARF_FOUND})
if (FOLLY_HAVE_ELF AND FOLLY_HAVE_DWARF AND FOLLY_HAVE_BACKTRACE AND LIBUNWIND_FOUND)
  set(FOLLY_USE_SYMBOLIZER ON)
endif()
message(STATUS "Setting FOLLY_USE_SYMBOLIZER: ${FOLLY_USE_SYMBOLIZER}")
message(STATUS "Setting FOLLY_HAVE_ELF: ${FOLLY_HAVE_ELF}")
message(STATUS "Setting FOLLY_HAVE_DWARF: ${FOLLY_HAVE_DWARF}")

# Using clang with libstdc++ requires explicitly linking against libatomic
check_cxx_source_compiles("
  #include <atomic>
  int main(int argc, char** argv) {
    struct Test { int val; };
    std::atomic<Test> s;
    return static_cast<int>(s.is_lock_free());
  }"
  FOLLY_CPP_ATOMIC_BUILTIN
)
if(NOT FOLLY_CPP_ATOMIC_BUILTIN)
  list(APPEND CMAKE_REQUIRED_LIBRARIES atomic)
  list(APPEND FOLLY_LINK_LIBRARIES atomic)
  check_cxx_source_compiles("
    #include <atomic>
    int main(int argc, char** argv) {
      struct Test { int val; };
      std::atomic<Test> s2;
      return static_cast<int>(s2.is_lock_free());
    }"
    FOLLY_CPP_ATOMIC_WITH_LIBATOMIC
  )
  if (NOT FOLLY_CPP_ATOMIC_WITH_LIBATOMIC)
    message(
      FATAL_ERROR "unable to link C++ std::atomic code: you may need \
      to install GNU libatomic"
    )
  endif()
endif()

option(
  FOLLY_LIBRARY_SANITIZE_ADDRESS
  "Build folly with Address Sanitizer enabled."
  OFF
)

if ($ENV{WITH_ASAN})
  message(STATUS "ENV WITH_ASAN is set")
  set (FOLLY_LIBRARY_SANITIZE_ADDRESS ON)
endif()

if (FOLLY_LIBRARY_SANITIZE_ADDRESS)
  if ("${CMAKE_CXX_COMPILER_ID}" MATCHES GNU)
    set(FOLLY_LIBRARY_SANITIZE_ADDRESS ON)
    set(FOLLY_ASAN_FLAGS -fsanitize=address,undefined)
    list(APPEND FOLLY_CXX_FLAGS ${FOLLY_ASAN_FLAGS})
    # All of the functions in folly/detail/Sse.cpp are intended to be compiled
    # with ASAN disabled.  They are marked with attributes to disable the
    # sanitizer, but even so, gcc fails to compile them for some reason when
    # sanitization is enabled on the compile line.
    set_source_files_properties(
      "${PROJECT_SOURCE_DIR}/folly/detail/Sse.cpp"
      PROPERTIES COMPILE_FLAGS -fno-sanitize=address,undefined
    )
  elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES Clang)
    set(FOLLY_LIBRARY_SANITIZE_ADDRESS ON)
    set(
      FOLLY_ASAN_FLAGS
      -fno-common
      -fsanitize=address,undefined,integer,nullability
      -fno-sanitize=unsigned-integer-overflow
    )
    list(APPEND FOLLY_CXX_FLAGS ${FOLLY_ASAN_FLAGS})
  endif()
endif()

add_library(folly_deps INTERFACE)

find_package(fmt CONFIG)
if (NOT DEFINED fmt_CONFIG)
    # Fallback on a normal search on the current system
    find_package(fmt MODULE REQUIRED)
endif()
target_link_libraries(folly_deps INTERFACE fmt::fmt)

list(REMOVE_DUPLICATES FOLLY_INCLUDE_DIRECTORIES)
target_include_directories(folly_deps INTERFACE ${FOLLY_INCLUDE_DIRECTORIES})
target_link_libraries(folly_deps INTERFACE
  ${FOLLY_LINK_LIBRARIES}
  ${FOLLY_SHINY_DEPENDENCIES}
  ${FOLLY_ASAN_FLAGS}
)
