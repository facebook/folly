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

cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
# We use the GoogleTest module if it is available (only in CMake 3.9+)
# It requires CMP0054 and CMP0057 to be enabled.
if (POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()
if (POLICY CMP0057)
  cmake_policy(SET CMP0057 NEW)
endif()

# CMP0075 Include file check macros honor CMAKE_REQUIRED_LIBRARIES
if(POLICY CMP0075)
  cmake_policy(SET CMP0075 NEW)
endif()

# CMP0167 The FindBoost module is removed
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# includes
set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
  # for in-fbsource builds
  "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
  # For shipit-transformed builds
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

# package information
set(PACKAGE_NAME      "folly")
if (NOT DEFINED PACKAGE_VERSION)
  set(PACKAGE_VERSION   "0.58.0-dev")
endif()
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/facebook/folly/issues")

# 150+ tests in the root folder anyone? No? I didn't think so.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(${PACKAGE_NAME} CXX C ASM)

# Set for FetchContent to skip find_package(folly)
set(folly_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  CACHE INTERNAL "folly source directory")

set(INCLUDE_INSTALL_DIR include CACHE STRING
    "The subdirectory where header files should be installed")
set(LIB_INSTALL_DIR lib CACHE STRING
    "The subdirectory where libraries should be installed")
set(BIN_INSTALL_DIR bin CACHE STRING
    "The subdirectory where binaries should be installed")
set(CMAKE_INSTALL_DIR lib/cmake/folly CACHE STRING
    "The subdirectory where CMake package config files should be installed")

option(BUILD_SHARED_LIBS
  "If enabled, build folly as a shared library.  \
  This is generally discouraged, since folly does not commit to having \
  a stable ABI."
  OFF
)
option(PYTHON_EXTENSIONS
  "Build Python Bindings for Folly, requires Cython and (BUILD_SHARED_LIBS=ON)"
  OFF
)
# Python extensions require shared libraries for proper symbol resolution
if (PYTHON_EXTENSIONS)
  set(BUILD_SHARED_LIBS ON CACHE BOOL
    "Forced ON because PYTHON_EXTENSIONS requires shared libraries" FORCE)
endif()
set(PYTHON_PACKAGE_INSTALL_DIR "" CACHE STRING
  "Installation directory for folly Python packages. \
  If empty, CMAKE_INSTALL_PREFIX will be used. \
  No effect if PYTHON_EXTENSIONS=OFF."
)
# Mark BUILD_SHARED_LIBS as an "advanced" option, since enabling it
# is generally discouraged.
mark_as_advanced(BUILD_SHARED_LIBS)
set(FOLLY_SUPPORT_SHARED_LIBRARY "${BUILD_SHARED_LIBS}")

include(FBBuildOptions)
fb_activate_static_library_option()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  message(STATUS "setting C++ standard to C++${CMAKE_CXX_STANDARD}")
endif()

if(NOT DEFINED IS_X86_64_ARCH AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64|AMD64")
  set(IS_X86_64_ARCH TRUE)
else()
  set(IS_X86_64_ARCH FALSE)
endif()

if(NOT DEFINED IS_AARCH64_ARCH AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
  set(IS_AARCH64_ARCH TRUE)
else()
  set(IS_AARCH64_ARCH FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Check target architecture
  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Folly requires a 64bit target architecture.")
  endif()

  if (MSVC_VERSION LESS 1900)
    message(
      FATAL_ERROR
      "This build script only supports building Folly on 64-bit Windows with "
      "at least Visual Studio 2017. "
      "MSVC version '${MSVC_VERSION}' is not supported."
    )
  endif()
endif()

if(NOT DEFINED FOLLY_HAVE_INT128_T)
  include(CheckTypeSize)
  check_type_size("__int128_t" FOLLY_INT128_T_SIZE)
  if(HAVE_FOLLY_INT128_T_SIZE)
    set(FOLLY_HAVE_INT128_T TRUE)
  endif()
endif()

set(TOP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FOLLY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/folly")
set(
  FOLLY_DIR_PREFIXES
  "${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}"
)
# https://gitlab.kitware.com/cmake/cmake/-/issues/18580#note_1405108
string(REGEX REPLACE "(.)" "\\\\\\1" FOLLY_DIR_REGEX_ESCAPED "${FOLLY_DIR}")

include(GNUInstallDirs)

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(FOLLY_HAVE_PTHREAD "${CMAKE_USE_PTHREADS_INIT}")
list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
list(APPEND FOLLY_LINK_LIBRARIES Threads::Threads)

if(MSVC)
  include(FollyCompilerMSVC)
else()
  include(FollyCompilerUnix)
endif()
include(FollyFunctions)

include(folly-deps) # Find the required packages
include(FollyConfigChecks)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/folly-config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
)

# Define FOLLY_XLOG_STRIP_PREFIXES when compiling our sources so that
# folly/logging will automatically choose the correct log category names,
# using only the relative portion of the source file name inside the
# folly repository.
set_property(
  DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}
  APPEND
  PROPERTY
  COMPILE_DEFINITIONS
  "FOLLY_XLOG_STRIP_PREFIXES=\"${CMAKE_SOURCE_DIR}:${CMAKE_BINARY_DIR}\""
)

# Collect headers for installation (still needed for install step)
auto_sources(hfiles "*.h" "RECURSE" "${FOLLY_DIR}")
REMOVE_MATCHES_FROM_LISTS(hfiles
  MATCHES
    "^${FOLLY_DIR_REGEX_ESCAPED}/build/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/docs/examples/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/logging/example/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/(.*/)?test/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/(.*/)?tool/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/facebook/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/rust/"
    "^${FOLLY_DIR_REGEX_ESCAPED}/ext/buck2/"
)
# Add test utility headers that are part of public API
list(APPEND hfiles
  ${FOLLY_DIR}/container/test/F14TestUtil.h
  ${FOLLY_DIR}/container/test/TrackingTypes.h
  ${FOLLY_DIR}/io/async/test/AsyncSSLSocketTest.h
  ${FOLLY_DIR}/io/async/test/AsyncSocketTest.h
  ${FOLLY_DIR}/io/async/test/AsyncSocketTest2.h
  ${FOLLY_DIR}/io/async/test/BlockingSocket.h
  ${FOLLY_DIR}/io/async/test/CallbackStateEnum.h
  ${FOLLY_DIR}/io/async/test/ConnCallback.h
  ${FOLLY_DIR}/io/async/test/MockAsyncSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncServerSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncSSLSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncTransport.h
  ${FOLLY_DIR}/io/async/test/MockAsyncUDPSocket.h
  ${FOLLY_DIR}/io/async/test/MockTimeoutManager.h
  ${FOLLY_DIR}/io/async/test/ScopedBoundPort.h
  ${FOLLY_DIR}/io/async/test/SocketPair.h
  ${FOLLY_DIR}/io/async/test/TFOUtil.h
  ${FOLLY_DIR}/io/async/test/TestSSLServer.h
  ${FOLLY_DIR}/io/async/test/TimeUtil.h
  ${FOLLY_DIR}/io/async/test/UndelayedDestruction.h
  ${FOLLY_DIR}/io/async/test/Util.h
  ${FOLLY_DIR}/synchronization/test/Semaphore.h
  ${FOLLY_DIR}/test/DeterministicSchedule.h
  ${FOLLY_DIR}/test/TestUtils.h
)

# Generate pkg-config variables from folly_deps before we add our own
# build/install-time include directory generator expressions
include(GenPkgConfig)
gen_pkgconfig_vars(FOLLY_PKGCONFIG folly_deps)

target_include_directories(folly_deps
  BEFORE
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_include_directories(folly_deps
  INTERFACE
    $<INSTALL_INTERFACE:include>
)

# =============================================================================
# Build granular libraries from subdirectory CMakeLists.txt files
# Sources are compiled ONCE via OBJECT libraries, then reused for both
# granular .a files and the monolithic libfolly.a
# =============================================================================

# Check for architecture-specific compiler flags needed by subdirectories
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mpclmul COMPILER_HAS_M_PCLMUL)

# GCC coroutines support
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  check_cxx_compiler_flag(-fcoroutines COMPILER_HAS_F_COROUTINES)
  if (COMPILER_HAS_F_COROUTINES)
    message(STATUS "GCC has C++ coroutines support, enabling for Folly.")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>)
  else()
    message(STATUS "GCC lacks C++ coroutines, disabling Folly coroutines.")
  endif()
endif()

add_subdirectory(folly)

# Create monolithic folly library from all component OBJECT libraries
folly_create_monolithic_library()

# Resolve deferred dependencies (must be after monolithic library is created)
folly_resolve_deferred_dependencies()

# Define FOLLY_CERTS_DIR for the granular test_util target
# This points to the SSL test certificates used by TestSSLServer and related tests
if(TARGET folly_testing_test_util)
  target_compile_definitions(folly_testing_test_util INTERFACE
    FOLLY_CERTS_DIR="${FOLLY_DIR}/io/async/test/certs"
  )
endif()

set(FOLLY_INSTALL_TARGETS folly folly_deps)

# Test utilities exported for use by downstream projects
add_library(folly_test_util
  ${FOLLY_DIR}/test/DeterministicSchedule.cpp
  ${FOLLY_DIR}/json/JsonTestUtil.cpp
)
target_compile_definitions(folly_test_util PUBLIC
  FOLLY_CERTS_DIR="${FOLLY_DIR}/io/async/test/certs"
)
set_property(TARGET folly_test_util PROPERTY VERSION ${PACKAGE_VERSION})
target_link_libraries(folly_test_util
  PUBLIC
    folly
    ${LIBGMOCK_LIBRARIES}
)
apply_folly_compile_options_to_target(folly_test_util)
list(APPEND FOLLY_INSTALL_TARGETS folly_test_util)

install(TARGETS ${FOLLY_INSTALL_TARGETS}
  EXPORT folly
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION ${LIB_INSTALL_DIR}
  ARCHIVE DESTINATION ${LIB_INSTALL_DIR})

auto_install_files(folly ${FOLLY_DIR}
  ${hfiles}
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
  DESTINATION ${INCLUDE_INSTALL_DIR}/folly
  COMPONENT dev
)

# Generate the folly-config.cmake file for installation so that
# downstream projects that use on folly can easily depend on it in their CMake
# files using "find_package(folly CONFIG)"
include(CMakePackageConfigHelpers)
configure_package_config_file(
  CMake/folly-config.cmake.in
  folly-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
  PATH_VARS
    INCLUDE_INSTALL_DIR
    CMAKE_INSTALL_DIR
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/folly-config.cmake
  DESTINATION ${CMAKE_INSTALL_DIR}
  COMPONENT dev
)
install(
  EXPORT folly
  DESTINATION ${CMAKE_INSTALL_DIR}
  NAMESPACE Folly::
  FILE folly-targets.cmake
  COMPONENT dev
)

# Generate a pkg-config file so that downstream projects that don't use
# CMake can depend on folly using pkg-config.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/libfolly.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/libfolly.pc.gen
  @ONLY
)

# Specify target to allow resolving generator expressions requiring
# a target for CMake >=3.19. See #1414.
# VERSION_GREATER_EQUAL isn't available before CMake 3.7.
if(NOT CMAKE_VERSION VERSION_LESS 3.19)
  set(target_arg TARGET folly_deps)
endif()

file(
  GENERATE
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libfolly.pc
  INPUT ${CMAKE_CURRENT_BINARY_DIR}/libfolly.pc.gen
  ${target_arg}
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/libfolly.pc
  DESTINATION ${LIB_INSTALL_DIR}/pkgconfig
  COMPONENT dev
)

option(BUILD_TESTS "If enabled, compile the tests." OFF)
option(BUILD_BENCHMARKS "If enabled, compile the benchmarks." OFF)
option(BUILD_BROKEN_TESTS "If enabled, compile tests that are known to be broken." OFF)
option(BUILD_HANGING_TESTS "If enabled, compile tests that are known to hang." OFF)
option(BUILD_SLOW_TESTS "If enabled, compile tests that take a while to run in debug mode." OFF)
if (BUILD_TESTS OR BUILD_BENCHMARKS)
  option(USE_CMAKE_GOOGLE_TEST_INTEGRATION "If enabled, use the google test integration included in CMake." ON)
  find_package(GMock MODULE REQUIRED)
  find_package(GTest MODULE REQUIRED)
  if (USE_CMAKE_GOOGLE_TEST_INTEGRATION)
    include(GoogleTest OPTIONAL RESULT_VARIABLE HAVE_CMAKE_GTEST)
    enable_testing()
  else()
    set(HAVE_CMAKE_GTEST OFF)
  endif()

  # The ThreadLocalTest code uses a helper shared library for one of its tests.
  # This can only be built if folly itself was built as a shared library.
  if (BUILD_SHARED_LIBS)
    add_library(thread_local_test_lib MODULE
      ${FOLLY_DIR}/test/ThreadLocalTestLib.cpp
    )
    set_target_properties(thread_local_test_lib PROPERTIES PREFIX "")
    apply_folly_compile_options_to_target(thread_local_test_lib)
    target_link_libraries(thread_local_test_lib PUBLIC folly)
    target_include_directories(
      thread_local_test_lib
      PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  add_library(folly_test_support
    ${FOLLY_DIR}/test/common/TestMain.cpp
    ${FOLLY_DIR}/test/FBVectorTestUtil.cpp
    ${FOLLY_DIR}/test/DeterministicSchedule.cpp
    ${FOLLY_DIR}/test/DeterministicSchedule.h
    ${FOLLY_DIR}/test/SingletonTestStructs.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.h
    ${FOLLY_DIR}/compression/test/CodingTestUtils.cpp
    ${FOLLY_DIR}/container/test/F14TestUtil.h
    ${FOLLY_DIR}/container/test/TrackingTypes.h
    ${FOLLY_DIR}/futures/test/TestExecutor.cpp
    ${FOLLY_DIR}/futures/test/TestExecutor.h
    ${FOLLY_DIR}/io/async/test/BlockingSocket.h
    ${FOLLY_DIR}/io/async/test/CallbackStateEnum.h
    ${FOLLY_DIR}/io/async/test/ConnCallback.h
    ${FOLLY_DIR}/io/async/test/MockAsyncServerSocket.h
    ${FOLLY_DIR}/io/async/test/MockAsyncSocket.h
    ${FOLLY_DIR}/io/async/test/MockAsyncSSLSocket.h
    ${FOLLY_DIR}/io/async/test/MockAsyncTransport.h
    ${FOLLY_DIR}/io/async/test/MockAsyncUDPSocket.h
    ${FOLLY_DIR}/io/async/test/MockTimeoutManager.h
    ${FOLLY_DIR}/io/async/test/ScopedBoundPort.cpp
    ${FOLLY_DIR}/io/async/test/ScopedBoundPort.h
    ${FOLLY_DIR}/io/async/test/SocketPair.cpp
    ${FOLLY_DIR}/io/async/test/SocketPair.h
    ${FOLLY_DIR}/io/async/test/SSLUtil.cpp
    ${FOLLY_DIR}/io/async/test/SSLUtil.h
    ${FOLLY_DIR}/io/async/test/TFOUtil.cpp
    ${FOLLY_DIR}/io/async/test/TFOUtil.h
    ${FOLLY_DIR}/io/async/test/TestSSLServer.cpp
    ${FOLLY_DIR}/io/async/test/TestSSLServer.h
    ${FOLLY_DIR}/io/async/test/TimeUtil.cpp
    ${FOLLY_DIR}/io/async/test/TimeUtil.h
    ${FOLLY_DIR}/io/async/test/UndelayedDestruction.h
    ${FOLLY_DIR}/io/async/test/Util.h
    ${FOLLY_DIR}/logging/test/ConfigHelpers.cpp
    ${FOLLY_DIR}/logging/test/ConfigHelpers.h
    ${FOLLY_DIR}/logging/test/TestLogHandler.cpp
    ${FOLLY_DIR}/logging/test/TestLogHandler.h
  )
  target_compile_definitions(folly_test_support
    PUBLIC
      ${LIBGMOCK_DEFINES}
  )
  target_include_directories(folly_test_support
    SYSTEM
    PUBLIC
      ${LIBGMOCK_INCLUDE_DIR}
      ${GTEST_INCLUDE_DIRS}
  )
  target_link_libraries(folly_test_support
    PUBLIC
      follybenchmark
      folly
      ${LIBGMOCK_LIBRARIES}
      glog::glog
  )
  apply_folly_compile_options_to_target(folly_test_support)

  # Test definitions live with their corresponding test directories.
  include(${FOLLY_DIR}/algorithm/simd/detail/test/CMakeLists.txt)
  include(${FOLLY_DIR}/algorithm/simd/test/CMakeLists.txt)
  include(${FOLLY_DIR}/chrono/test/CMakeLists.txt)
  include(${FOLLY_DIR}/cli/test/CMakeLists.txt)
  include(${FOLLY_DIR}/codec/test/CMakeLists.txt)
  include(${FOLLY_DIR}/compression/test/CMakeLists.txt)
  include(${FOLLY_DIR}/compression/elias_fano/test/CMakeLists.txt)
  include(${FOLLY_DIR}/container/test/CMakeLists.txt)
  include(${FOLLY_DIR}/concurrency/container/test/CMakeLists.txt)
  include(${FOLLY_DIR}/concurrency/test/CMakeLists.txt)
  include(${FOLLY_DIR}/detail/test/CMakeLists.txt)
  include(${FOLLY_DIR}/detail/base64_detail/test/CMakeLists.txt)
  include(${FOLLY_DIR}/executors/test/CMakeLists.txt)
  include(${FOLLY_DIR}/executors/task_queue/test/CMakeLists.txt)
  include(${FOLLY_DIR}/external/farmhash/test/CMakeLists.txt)
  include(${FOLLY_DIR}/logging/test/CMakeLists.txt)
  include(${FOLLY_DIR}/fibers/test/CMakeLists.txt)
  include(${FOLLY_DIR}/functional/test/CMakeLists.txt)
  include(${FOLLY_DIR}/futures/test/CMakeLists.txt)
  include(${FOLLY_DIR}/gen/test/CMakeLists.txt)
  include(${FOLLY_DIR}/hash/test/CMakeLists.txt)
  include(${FOLLY_DIR}/io/test/CMakeLists.txt)
  include(${FOLLY_DIR}/io/async/test/CMakeLists.txt)
  include(${FOLLY_DIR}/io/async/ssl/test/CMakeLists.txt)
  include(${FOLLY_DIR}/lang/test/CMakeLists.txt)
  include(${FOLLY_DIR}/memory/test/CMakeLists.txt)
  include(${FOLLY_DIR}/net/detail/test/CMakeLists.txt)
  include(${FOLLY_DIR}/portability/test/CMakeLists.txt)
  include(${FOLLY_DIR}/ssl/test/CMakeLists.txt)
  include(${FOLLY_DIR}/stats/test/CMakeLists.txt)
  include(${FOLLY_DIR}/synchronization/test/CMakeLists.txt)
  include(${FOLLY_DIR}/synchronization/detail/test/CMakeLists.txt)
  include(${FOLLY_DIR}/system/test/CMakeLists.txt)
  include(${FOLLY_DIR}/test/CMakeLists.txt)
  include(${FOLLY_DIR}/testing/test/CMakeLists.txt)
  include(${FOLLY_DIR}/tracing/test/CMakeLists.txt)
  include(${FOLLY_DIR}/json/test/CMakeLists.txt)
  include(${FOLLY_DIR}/crypto/test/CMakeLists.txt)



endif()
