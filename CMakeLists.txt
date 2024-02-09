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

cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)
# We use the GoogleTest module if it is available (only in CMake 3.9+)
# It requires CMP0054 and CMP0057 to be enabled.

IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
  MESSAGE(STATUS "current platform: Linux ")
ELSEIF (CMAKE_SYSTEM_NAME MATCHES "Windows")
  MESSAGE(STATUS "current platform: Windows")
ELSEIF (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  MESSAGE(STATUS "current platform: FreeBSD")
ELSEIF (CMAKE_SYSTEM_NAME MATCHES "Darwin")
  MESSAGE(STATUS "current platform: MacOS")
  set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl" )
  set(OPENSSL_LIBRARIES "/usr/local/opt/openssl/lib" )
  set(OPENSSL_INCLUDE_DIR "/usr/local/opt/openssl/include" )
ELSE ()
  MESSAGE(STATUS "other platform: ${CMAKE_SYSTEM_NAME}")
ENDIF (CMAKE_SYSTEM_NAME MATCHES "Linux")


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
# Mark BUILD_SHARED_LIBS as an "advanced" option, since enabling it
# is generally discouraged.
mark_as_advanced(BUILD_SHARED_LIBS)
set(FOLLY_SUPPORT_SHARED_LIBRARY "${BUILD_SHARED_LIBS}")

include(FBBuildOptions)
fb_activate_static_library_option()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  message(STATUS "setting C++ standard to C++${CMAKE_CXX_STANDARD}")
endif()

if(NOT DEFINED IS_X86_64_ARCH AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64|AMD64")
  set(IS_X86_64_ARCH TRUE)
else()
  set(IS_X86_64_ARCH FALSE)
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

set(TOP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FOLLY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/folly")
set(
  FOLLY_DIR_PREFIXES
  "${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}"
)

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

# We currently build the main libfolly library by finding all sources
# and header files.  We then exclude specific files below.
#
# In the future it would perhaps be nicer to explicitly list the files we want
# to include, and to move the source lists in to separate per-subdirectory
# CMakeLists.txt files.
auto_sources(files "*.cpp" "RECURSE" "${FOLLY_DIR}")
auto_sources(hfiles "*.h" "RECURSE" "${FOLLY_DIR}")

# Exclude tests, benchmarks, and other standalone utility executables from the
# library sources.  Test sources are listed separately below.
REMOVE_MATCHES_FROM_LISTS(files hfiles
  MATCHES
    "^${FOLLY_DIR}/build/"
    "^${FOLLY_DIR}/docs/examples/"
    "^${FOLLY_DIR}/logging/example/"
    "^${FOLLY_DIR}/(.*/)?test/"
    "^${FOLLY_DIR}/(.*/)?tool/"
    "^${FOLLY_DIR}/facebook/"
    "Benchmark.cpp$"
    "Test.cpp$"
)

# Exclude exception tracer, which is necessary to statically link libstdc++
if (${FOLLY_NO_EXCEPTION_TRACER})
  REMOVE_MATCHES_FROM_LISTS(files hfiles
    MATCHES
      "^${FOLLY_DIR}/experimental/exception_tracer/"
  )
endif()

list(REMOVE_ITEM files
  ${FOLLY_DIR}/python/error.cpp
  ${FOLLY_DIR}/python/executor.cpp
  ${FOLLY_DIR}/python/fibers.cpp
  ${FOLLY_DIR}/python/GILAwareManualExecutor.cpp
  ${FOLLY_DIR}/python/iobuf.cpp
  ${FOLLY_DIR}/python/iobuf_ext.cpp
  ${FOLLY_DIR}/python/ProactorExecutor.cpp
)
list(REMOVE_ITEM hfiles
  ${FOLLY_DIR}/python/fibers.h
  ${FOLLY_DIR}/python/GILAwareManualExecutor.h
  ${FOLLY_DIR}/python/iobuf_ext.h
  ${FOLLY_DIR}/python/ProactorExecutor.h
)

# Explicitly include utility library code from inside
# test subdirs
list(APPEND files
  ${FOLLY_DIR}/io/async/test/ScopedBoundPort.cpp
  ${FOLLY_DIR}/io/async/test/SocketPair.cpp
  ${FOLLY_DIR}/io/async/test/TimeUtil.cpp
)
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

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  check_cxx_compiler_flag(-fcoroutines COMPILER_HAS_F_COROUTINES)
  if (COMPILER_HAS_F_COROUTINES)
    message(
      STATUS
      "GCC has support for C++ coroutines, setting flag for Folly build."
    )
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>)
  else()
    message(
      STATUS
      "GCC does not have support for C++ coroutines, "
      "disabling Folly coroutine support."
    )
  endif()
endif()

# base64 SIMD files compilation
if (NOT(${IS_X86_64_ARCH}))
  message(
    STATUS
    "arch ${CMAKE_LIBRARY_ARCHITECTURE} does not match x86_64, "
    "skipping building SSE4.2 version of base64"
  )
  list(REMOVE_ITEM files ${FOLLY_DIR}/detail/base64_detail/Base64_SSE4_2.cpp)
else()
  message(
    STATUS
    "arch ${CMAKE_LIBRARY_ARCHITECTURE} matches x86_64, "
    "building SSE4.2 version of base64"
  )
  # MSVC does not have a way to enable just sse4.2, only avx.
  # If we don't pass the flag, everything will still work but no warnings
  if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_source_files_properties(
      ${FOLLY_DIR}/detail/base64_detail/Base64_SSE4_2.cpp
      PROPERTIES
      COMPILE_FLAGS
        -msse4.2
    )
  endif()
endif()

if (${LIBSODIUM_FOUND})
  if (NOT(${IS_X86_64_ARCH}))
    message(
      STATUS
      "arch ${CMAKE_LIBRARY_ARCHITECTURE} does not match x86_64, "
      "skipping setting SSE2/AVX2 compile flags for LtHash SIMD code"
    )
  else()
    message(
      STATUS
      "arch ${CMAKE_LIBRARY_ARCHITECTURE} matches x86_64, "
      "setting SSE2/AVX2 compile flags for LtHash SIMD code"
    )
    set_source_files_properties(
      ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_AVX2.cpp
      PROPERTIES
      COMPILE_FLAGS
      -mavx -mavx2 -msse2
    )
    set_source_files_properties(
      ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_Simple.cpp
      PROPERTIES
      COMPILE_FLAGS
      -mno-avx -mno-avx2 -mno-sse2
    )
    set_source_files_properties(
      ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_SSE2.cpp
      PROPERTIES
      COMPILE_FLAGS
      -mno-avx -mno-avx2 -msse2
    )
  endif()
else()
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/experimental/crypto/Blake2xb.cpp
    ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_AVX2.cpp
    ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_Simple.cpp
    ${FOLLY_DIR}/experimental/crypto/detail/MathOperation_SSE2.cpp
    ${FOLLY_DIR}/experimental/crypto/LtHash.cpp
  )
  list(REMOVE_ITEM hfiles
    ${FOLLY_DIR}/experimental/crypto/Blake2xb.h
    ${FOLLY_DIR}/experimental/crypto/detail/LtHashInternal.h
    ${FOLLY_DIR}/experimental/crypto/LtHash-inl.h
    ${FOLLY_DIR}/experimental/crypto/LtHash.h
  )
endif()
if (NOT ${LIBGFLAGS_FOUND})
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/experimental/NestedCommandLineApp.cpp
    ${FOLLY_DIR}/experimental/ProgramOptions.cpp
  )
  list(REMOVE_ITEM hfiles
    ${FOLLY_DIR}/experimental/NestedCommandLineApp.h
    ${FOLLY_DIR}/experimental/ProgramOptions.h
  )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/Subprocess.cpp
  )
endif()

set(PCLMUL_FILES
  ${FOLLY_DIR}/hash/detail/ChecksumDetail.cpp
  ${FOLLY_DIR}/hash/detail/Crc32CombineDetail.cpp
  ${FOLLY_DIR}/hash/detail/Crc32cDetail.cpp
)
check_cxx_compiler_flag(-mpclmul COMPILER_HAS_M_PCLMUL)
if (COMPILER_HAS_M_PCLMUL)
  message(
    STATUS
    "compiler has flag pclmul, setting compile flag for ${PCLMUL_FILES}"
  )
  set_source_files_properties(
    ${PCLMUL_FILES}
    PROPERTIES
    COMPILE_OPTIONS
    -mpclmul
  )
else()
  message(
    STATUS
    "compiler does not have flag pclmul, skipping setting compile flags for ${PCLMUL_FILES}"
  )
endif()

list(APPEND folly_base_files
  ${files} ${hfiles}
)

if (IS_X86_64_ARCH AND NOT MSVC)
  set_property(
    SOURCE
    ${FOLLY_DIR}/memcpy.S
    APPEND PROPERTY COMPILE_OPTIONS "-x" "assembler-with-cpp"
  )
  list(APPEND folly_base_files
    ${FOLLY_DIR}/memcpy.S
  )
endif()

add_library(folly_base OBJECT
  ${folly_base_files}
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
)
if (BUILD_SHARED_LIBS)
  set_property(TARGET folly_base PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()
auto_source_group(folly ${FOLLY_DIR} ${files} ${hfiles})
apply_folly_compile_options_to_target(folly_base)
# Add the generated files to the correct source group.
source_group("folly" FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h)

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

target_include_directories(folly_base
  PUBLIC
    $<TARGET_PROPERTY:folly_deps,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(folly_base
  PUBLIC
    $<TARGET_PROPERTY:folly_deps,INTERFACE_COMPILE_DEFINITIONS>
)

set(FOLLY_INSTALL_TARGETS folly folly_deps)

option(PYTHON_EXTENSIONS
  "Build Python Bindings for Folly, requires Cython and (BUILD_SHARED_LIBS=ON)"
  OFF
)

add_library(folly
  $<TARGET_OBJECTS:folly_base>
)
set_property(TARGET folly PROPERTY VERSION ${PACKAGE_VERSION})
apply_folly_compile_options_to_target(folly)
target_compile_features(folly INTERFACE cxx_generic_lambdas)

target_link_libraries(folly PUBLIC folly_deps)

# Test utilities exported for use by downstream projects
add_library(folly_test_util
  ${FOLLY_DIR}/test/DeterministicSchedule.cpp
  ${FOLLY_DIR}/json/JsonTestUtil.cpp
)
set_property(TARGET folly_test_util PROPERTY VERSION ${PACKAGE_VERSION})
target_link_libraries(folly_test_util
  PUBLIC
    ${BOOST_LIBRARIES}
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
    ${FOLLY_DIR}/container/test/F14TestUtil.h
    ${FOLLY_DIR}/container/test/TrackingTypes.h
    ${FOLLY_DIR}/experimental/test/CodingTestUtils.cpp
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
      ${BOOST_LIBRARIES}
      follybenchmark
      folly
      ${LIBGMOCK_LIBRARIES}
      ${GLOG_LIBRARY}
  )
  apply_folly_compile_options_to_target(folly_test_support)

  folly_define_tests(
    DIRECTORY chrono/test/
      TEST chrono_conv_test WINDOWS_DISABLED
        SOURCES ConvTest.cpp

    DIRECTORY compression/test/
      TEST compression_test SLOW SOURCES CompressionTest.cpp

    DIRECTORY container/test/
      TEST access_test SOURCES AccessTest.cpp
      TEST array_test SOURCES ArrayTest.cpp
      BENCHMARK bit_iterator_benchmark SOURCES BitIteratorBench.cpp
      TEST bit_iterator_test SOURCES BitIteratorTest.cpp
      TEST enumerate_test SOURCES EnumerateTest.cpp
      BENCHMARK evicting_cache_map_benchmark SOURCES EvictingCacheMapBench.cpp
      TEST evicting_cache_map_test SOURCES EvictingCacheMapTest.cpp
      TEST f14_fwd_test SOURCES F14FwdTest.cpp
      TEST f14_map_test SOURCES F14MapTest.cpp
      TEST f14_set_test WINDOWS_DISABLED SOURCES F14SetTest.cpp
      TEST heap_vector_types_test SOURCES heap_vector_types_test.cpp
      BENCHMARK foreach_benchmark SOURCES ForeachBenchmark.cpp
      TEST foreach_test SOURCES ForeachTest.cpp
      TEST merge_test SOURCES MergeTest.cpp
      BENCHMARK sparse_byte_set_benchmark SOURCES SparseByteSetBenchmark.cpp
      TEST sparse_byte_set_test SOURCES SparseByteSetTest.cpp
      TEST util_test SOURCES UtilTest.cpp

    DIRECTORY concurrency/test/
      TEST atomic_shared_ptr_test SOURCES AtomicSharedPtrTest.cpp
      TEST cache_locality_test WINDOWS_DISABLED SOURCES CacheLocalityTest.cpp
      TEST core_cached_shared_ptr_test SOURCES CoreCachedSharedPtrTest.cpp
      BENCHMARK concurrent_hash_map_benchmark WINDOWS_DISABLED
        SOURCES ConcurrentHashMapBench.cpp
      TEST concurrent_hash_map_test WINDOWS_DISABLED
        SOURCES ConcurrentHashMapTest.cpp
      TEST dynamic_bounded_queue_test WINDOWS_DISABLED
        SOURCES DynamicBoundedQueueTest.cpp
      TEST priority_unbounded_queue_set_test
        SOURCES PriorityUnboundedQueueSetTest.cpp
      BENCHMARK thread_cached_synchronized_benchmark
        SOURCES ThreadCachedSynchronizedBench.cpp
      TEST thread_cached_synchronized_test
        SOURCES ThreadCachedSynchronizedTest.cpp
      TEST unbounded_queue_test SOURCES UnboundedQueueTest.cpp

    DIRECTORY detail/test/
      TEST static_singleton_manager_test SOURCES StaticSingletonManagerTest.cpp

    DIRECTORY detail/test/
      TEST simd_for_each_test SOURCES SimdForEachTest.cpp

    DIRECTORY detail/test/
      TEST simple_simd_string_utils_test SOURCES SimpleSimdStringUtilsTest.cpp

    DIRECTORY detail/test/
      TEST split_string_simd_test SOURCES SplitStringSimdTest.cpp

    DIRECTORY detail/test/
      TEST unroll_utils_test SOURCES UnrollUtilsTest.cpp

    DIRECTORY detail/base64_detail/tests/
      TEST base64_detail_test
      SOURCES
        Base64AgainstScalarTest.cpp
        Base64PlatformTest.cpp
        Base64SpecialCasesTest.cpp

    DIRECTORY executors/test/
      TEST async_helpers_test SOURCES AsyncTest.cpp
      TEST codel_test WINDOWS_DISABLED SOURCES CodelTest.cpp
      BENCHMARK edf_thread_pool_executor_benchmark
        SOURCES EDFThreadPoolExecutorBenchmark.cpp
      TEST executor_test SOURCES ExecutorTest.cpp
      TEST fiber_io_executor_test WINDOWS_DISABLED
        SOURCES FiberIOExecutorTest.cpp
      TEST global_executor_test SOURCES GlobalExecutorTest.cpp
      TEST serial_executor_test SOURCES SerialExecutorTest.cpp
      # Fails in ThreadPoolExecutorTest.RequestContext:719 data2 != nullptr
      TEST thread_pool_executor_test BROKEN WINDOWS_DISABLED
        SOURCES ThreadPoolExecutorTest.cpp
      TEST threaded_executor_test SOURCES ThreadedExecutorTest.cpp
      TEST timed_drivable_executor_test SOURCES TimedDrivableExecutorTest.cpp

    DIRECTORY executors/task_queue/test/
      TEST priority_unbounded_blocking_queue_test
        SOURCES PriorityUnboundedBlockingQueueTest.cpp
      BENCHMARK unbounded_blocking_queue_benchmark
        SOURCES UnboundedBlockingQueueBench.cpp
      TEST unbounded_blocking_queue_test SOURCES UnboundedBlockingQueueTest.cpp

    DIRECTORY experimental/test/
      TEST autotimer_test SOURCES AutoTimerTest.cpp
      TEST bits_test_2 SOURCES BitsTest.cpp
      TEST bitvector_test SOURCES BitVectorCodingTest.cpp
      TEST dynamic_parser_test SOURCES DynamicParserTest.cpp
      TEST eliasfano_test SOURCES EliasFanoCodingTest.cpp
      TEST event_count_test SOURCES EventCountTest.cpp
      # FunctionSchedulerTest has a lot of timing-dependent checks,
      # and tends to fail on heavily loaded systems.
      TEST function_scheduler_test BROKEN SOURCES FunctionSchedulerTest.cpp
      TEST future_dag_test SOURCES FutureDAGTest.cpp
      TEST json_schema_test SOURCES JSONSchemaTest.cpp
      TEST lock_free_ring_buffer_test SOURCES LockFreeRingBufferTest.cpp
      #TEST nested_command_line_app_test SOURCES NestedCommandLineAppTest.cpp
      #TEST program_options_test SOURCES ProgramOptionsTest.cpp
      TEST quotient_multiset_test SOURCES QuotientMultiSetTest.cpp
      # Depends on liburcu
      #TEST read_mostly_shared_ptr_test SOURCES ReadMostlySharedPtrTest.cpp
      #TEST ref_count_test SOURCES RefCountTest.cpp
      TEST select64_test SOURCES Select64Test.cpp
      BENCHMARK string_keyed_benchmark SOURCES StringKeyedBenchmark.cpp
      TEST stringkeyed_test SOURCES StringKeyedTest.cpp
      TEST test_util_test SOURCES TestUtilTest.cpp
      TEST tuple_ops_test SOURCES TupleOpsTest.cpp

    DIRECTORY experimental/io/test/
      TEST fs_util_test SOURCES FsUtilTest.cpp

    DIRECTORY external/farmhash/test/
      TEST farmhash_test SOURCES farmhash_test.cpp

    DIRECTORY logging/test/
      TEST async_file_writer_test WINDOWS_DISABLED
        SOURCES AsyncFileWriterTest.cpp
      TEST config_parser_test SOURCES ConfigParserTest.cpp
      TEST config_update_test SOURCES ConfigUpdateTest.cpp
      TEST file_handler_factory_test WINDOWS_DISABLED
        SOURCES FileHandlerFactoryTest.cpp
      TEST glog_formatter_test SOURCES GlogFormatterTest.cpp
      TEST immediate_file_writer_test SOURCES ImmediateFileWriterTest.cpp
      TEST log_category_test SOURCES LogCategoryTest.cpp
      TEST logger_db_test SOURCES LoggerDBTest.cpp
      TEST logger_test WINDOWS_DISABLED SOURCES LoggerTest.cpp
      TEST log_level_test SOURCES LogLevelTest.cpp
      TEST log_message_test SOURCES LogMessageTest.cpp
      TEST log_name_test SOURCES LogNameTest.cpp
      TEST log_stream_test SOURCES LogStreamTest.cpp
      TEST rate_limiter_test SOURCES RateLimiterTest.cpp
      TEST standard_log_handler_test SOURCES StandardLogHandlerTest.cpp
      TEST xlog_test WINDOWS_DISABLED
        HEADERS
          XlogHeader1.h
          XlogHeader2.h
        SOURCES
          XlogFile1.cpp
          XlogFile2.cpp
          XlogTest.cpp

    DIRECTORY fibers/test/
      # FiberManager swapWithException fails with segfault
      BENCHMARK fibers_benchmark SOURCES FibersBenchmark.cpp
      TEST fibers_test BROKEN SOURCES FibersTest.cpp

    DIRECTORY functional/test/
      TEST apply_tuple_test WINDOWS_DISABLED
        SOURCES ApplyTupleTest.cpp
      TEST functional_invoke_test SOURCES InvokeTest.cpp
      TEST partial_test SOURCES PartialTest.cpp
      TEST functional_protocol_test SOURCES protocol_test.cpp
      TEST functional_traits_test SOURCES traits_test.cpp

    DIRECTORY futures/test/
      TEST barrier_test SOURCES BarrierTest.cpp
      TEST callback_lifetime_test SOURCES CallbackLifetimeTest.cpp
      TEST collect_test SOURCES CollectTest.cpp
      TEST context_test SOURCES ContextTest.cpp
      TEST core_test SOURCES CoreTest.cpp
      TEST ensure_test SOURCES EnsureTest.cpp
      TEST filter_test SOURCES FilterTest.cpp
      TEST future_splitter_test SOURCES FutureSplitterTest.cpp
      BENCHMARK futures_benchmark WINDOWS_DISABLED
        SOURCES Benchmark.cpp
      TEST future_test WINDOWS_DISABLED
        SOURCES FutureTest.cpp
      TEST header_compile_test SOURCES HeaderCompileTest.cpp
      TEST interrupt_test SOURCES InterruptTest.cpp
      TEST map_test SOURCES MapTest.cpp
      TEST non_copyable_lambda_test SOURCES NonCopyableLambdaTest.cpp
      TEST poll_test SOURCES PollTest.cpp
      TEST promise_test SOURCES PromiseTest.cpp
      TEST reduce_test SOURCES ReduceTest.cpp
      TEST retrying_test SOURCES RetryingTest.cpp
      TEST self_destruct_test SOURCES SelfDestructTest.cpp
      TEST shared_promise_test SOURCES SharedPromiseTest.cpp
      TEST test_executor_test SOURCES TestExecutorTest.cpp
      TEST then_compile_test
        HEADERS
          ThenCompileTest.h
        SOURCES
          ThenCompileTest.cpp
      TEST then_test SOURCES ThenTest.cpp
      TEST timekeeper_test WINDOWS_DISABLED SOURCES TimekeeperTest.cpp
      TEST times_test SOURCES TimesTest.cpp
      TEST unwrap_test SOURCES UnwrapTest.cpp
      TEST via_test SOURCES ViaTest.cpp
      TEST wait_test SOURCES WaitTest.cpp
      TEST when_test SOURCES WhenTest.cpp
      TEST while_do_test SOURCES WhileDoTest.cpp
      TEST will_equal_test SOURCES WillEqualTest.cpp
      TEST window_test WINDOWS_DISABLED
        SOURCES WindowTest.cpp

    DIRECTORY gen/test/
      # MSVC bug can't resolve initializer_list constructor properly
      #TEST base_test SOURCES BaseTest.cpp
      TEST combine_test SOURCES CombineTest.cpp
      BENCHMARK parallel_map_benchmark SOURCES ParallelMapBenchmark.cpp
      TEST parallel_map_test SOURCES ParallelMapTest.cpp
      BENCHMARK parallel_benchmark SOURCES ParallelBenchmark.cpp
      TEST parallel_test SOURCES ParallelTest.cpp

    DIRECTORY hash/test/
      BENCHMARK checksum_benchmark SOURCES ChecksumBenchmark.cpp
      TEST checksum_test SOURCES ChecksumTest.cpp
      TEST farm_hash_test SOURCES FarmHashTest.cpp
      BENCHMARK hash_benchmark WINDOWS_DISABLED
        SOURCES HashBenchmark.cpp
      TEST hash_test WINDOWS_DISABLED
        SOURCES HashTest.cpp
      TEST spooky_hash_v1_test SOURCES SpookyHashV1Test.cpp
      TEST spooky_hash_v2_test SOURCES SpookyHashV2Test.cpp

    DIRECTORY io/test/
      TEST iobuf_test WINDOWS_DISABLED SOURCES IOBufTest.cpp
      TEST iobuf_cursor_test SOURCES IOBufCursorTest.cpp
      TEST iobuf_queue_test SOURCES IOBufQueueTest.cpp
      TEST record_io_test WINDOWS_DISABLED SOURCES RecordIOTest.cpp
      TEST ShutdownSocketSetTest HANGING
        SOURCES ShutdownSocketSetTest.cpp
      TEST SocketOptionValueTest HANGING
        SOURCES SocketOptionValueTest.cpp

    DIRECTORY io/async/test/
      # A number of tests in the async_test binary are unfortunately flaky.
      # When run under Travis CI a number of the tests also hang (it looks
      # like they do not get expected socket accept events, causing them
      # to never break out of their event loops).
      TEST async_test BROKEN
        CONTENT_DIR certs/
        HEADERS
          AsyncSocketTest.h
          AsyncSSLSocketTest.h
        SOURCES
          AsyncPipeTest.cpp
          AsyncSocketExceptionTest.cpp
          AsyncSocketTest.cpp
          AsyncSocketTest2.cpp
          AsyncSSLSocketTest.cpp
          AsyncSSLSocketTest2.cpp
          AsyncSSLSocketWriteTest.cpp
          AsyncTransportTest.cpp
          # This is disabled because it depends on things that don't exist
          # on Windows.
          #EventHandlerTest.cpp
          # The async signal handler is not supported on Windows.
          #AsyncSignalHandlerTest.cpp
      TEST async_timeout_test SOURCES AsyncTimeoutTest.cpp
      TEST AsyncUDPSocketTest APPLE_DISABLED WINDOWS_DISABLED
        SOURCES AsyncUDPSocketTest.cpp
      TEST DelayedDestructionTest SOURCES DelayedDestructionTest.cpp
      TEST DelayedDestructionBaseTest SOURCES DelayedDestructionBaseTest.cpp
      TEST DestructorCheckTest SOURCES DestructorCheckTest.cpp
      # Fails with gtest macro error
      TEST EventBaseTest BROKEN SOURCES EventBaseTest.cpp
      TEST EventBaseLocalTest WINDOWS_DISABLED SOURCES EventBaseLocalTest.cpp
      TEST HHWheelTimerTest SOURCES HHWheelTimerTest.cpp
      TEST HHWheelTimerSlowTests SLOW
        SOURCES HHWheelTimerSlowTests.cpp
      TEST NotificationQueueTest WINDOWS_DISABLED
        SOURCES NotificationQueueTest.cpp
      BENCHMARK request_context_benchmark WINDOWS_DISABLED
        SOURCES RequestContextBenchmark.cpp
        HEADERS RequestContextHelper.h
      TEST RequestContextTest WINDOWS_DISABLED SOURCES RequestContextTest.cpp
      TEST ScopedEventBaseThreadTest WINDOWS_DISABLED
        SOURCES ScopedEventBaseThreadTest.cpp
      TEST ssl_session_test
        CONTENT_DIR certs/
        SOURCES SSLSessionTest.cpp
      TEST writechain_test SOURCES WriteChainAsyncTransportWrapperTest.cpp

    DIRECTORY io/async/ssl/test/
      TEST ssl_errors_test SOURCES SSLErrorsTest.cpp

    DIRECTORY lang/test/
      TEST lang_aligned_test SOURCES AlignedTest.cpp
      TEST lang_badge_test SOURCES BadgeTest.cpp
      TEST lang_bits_test SOURCES BitsTest.cpp
      TEST lang_byte_test SOURCES ByteTest.cpp
      TEST lang_c_string_test SOURCES CStringTest.cpp
      TEST lang_cast_test SOURCES CastTest.cpp
      TEST lang_checked_math_test SOURCES CheckedMathTest.cpp
      TEST lang_exception_test SOURCES ExceptionTest.cpp
      TEST lang_extern_test SOURCES ExternTest.cpp
      TEST lang_launder_test SOURCES LaunderTest.cpp
      TEST lang_ordering_test SOURCES OrderingTest.cpp
      TEST lang_pretty_test SOURCES PrettyTest.cpp
      TEST lang_propagate_const_test SOURCES PropagateConstTest.cpp
      TEST lang_r_value_reference_wrapper_test WINDOWS_DISABLED
        SOURCES RValueReferenceWrapperTest.cpp
      TEST lang_safe_assert_test SOURCES SafeAssertTest.cpp
      BENCHMARK lang_to_ascii_benchmark SOURCES ToAsciiBench.cpp
      TEST lang_to_ascii_test SOURCES ToAsciiTest.cpp
      TEST lang_type_info_test SOURCES TypeInfoTest.cpp
      TEST lang_uncaught_exceptions_test SOURCES UncaughtExceptionsTest.cpp

    DIRECTORY memory/test/
      TEST arena_test WINDOWS_DISABLED SOURCES ArenaTest.cpp
      TEST reentrant_allocator_test WINDOWS_DISABLED
        SOURCES ReentrantAllocatorTest.cpp
      TEST thread_cached_arena_test WINDOWS_DISABLED
        SOURCES ThreadCachedArenaTest.cpp
      TEST mallctl_helper_test SOURCES MallctlHelperTest.cpp
      TEST uninitialized_memory_hacks_test
        SOURCES UninitializedMemoryHacksTest.cpp

    DIRECTORY net/detail/test/
      TEST socket_file_descriptor_map_test SOURCES SocketFileDescriptorMapTest.cpp

    DIRECTORY portability/test/
      TEST constexpr_test SOURCES ConstexprTest.cpp
      TEST filesystem_test SOURCES FilesystemTest.cpp
      TEST libgen-test SOURCES LibgenTest.cpp
      TEST openssl_portability_test SOURCES OpenSSLPortabilityTest.cpp
      TEST pthread_test SOURCES PThreadTest.cpp
      TEST time-test SOURCES TimeTest.cpp

    DIRECTORY ssl/test/
      TEST openssl_hash_test SOURCES OpenSSLHashTest.cpp

    DIRECTORY stats/test/
      TEST buffered_stat_test SOURCES BufferedStatTest.cpp
      BENCHMARK digest_builder_benchmark SOURCES DigestBuilderBenchmark.cpp
      TEST digest_builder_test SOURCES DigestBuilderTest.cpp
      BENCHMARK histogram_benchmark SOURCES HistogramBenchmark.cpp
      TEST histogram_test SOURCES HistogramTest.cpp
      BENCHMARK quantile_histogram_benchmark
        SOURCES QuantileHistogramBenchmark.cpp
      TEST quantile_estimator_test SOURCES QuantileEstimatorTest.cpp
      TEST sliding_window_test SOURCES SlidingWindowTest.cpp
      BENCHMARK tdigest_benchmark SOURCES TDigestBenchmark.cpp
      TEST tdigest_test SOURCES TDigestTest.cpp
      TEST timeseries_histogram_test SOURCES TimeseriesHistogramTest.cpp
      TEST timeseries_test SOURCES TimeSeriesTest.cpp

    DIRECTORY synchronization/test/
      BENCHMARK baton_benchmark SOURCES BatonBenchmark.cpp
      TEST baton_test SOURCES BatonTest.cpp
      TEST call_once_test SOURCES CallOnceTest.cpp
      TEST lifo_sem_test WINDOWS_DISABLED SOURCES LifoSemTests.cpp
      TEST relaxed_atomic_test WINDOWS_DISABLED SOURCES RelaxedAtomicTest.cpp
      TEST rw_spin_lock_test SOURCES RWSpinLockTest.cpp
      TEST semaphore_test WINDOWS_DISABLED SOURCES SemaphoreTest.cpp

    DIRECTORY synchronization/detail/test/
      TEST hardware_test SOURCES HardwareTest.cpp

    DIRECTORY system/test/
      TEST memory_mapping_test SOURCES MemoryMappingTest.cpp
      TEST shell_test SOURCES ShellTest.cpp
      #TEST subprocess_test SOURCES SubprocessTest.cpp
      TEST thread_id_test SOURCES ThreadIdTest.cpp
      TEST thread_name_test WINDOWS_DISABLED
        SOURCES ThreadNameTest.cpp

    DIRECTORY synchronization/test/
      TEST atomic_struct_test SOURCES AtomicStructTest.cpp
      TEST small_locks_test SOURCES SmallLocksTest.cpp
      TEST atomic_util_test SOURCES AtomicUtilTest.cpp

    DIRECTORY test/
      TEST ahm_int_stress_test SOURCES AHMIntStressTest.cpp
      TEST arena_smartptr_test SOURCES ArenaSmartPtrTest.cpp
      BENCHMARK ascii_case_insensitive_benchmark
        SOURCES AsciiCaseInsensitiveBenchmark.cpp
      TEST ascii_check_test SOURCES AsciiCaseInsensitiveTest.cpp
      TEST atomic_hash_array_test SOURCES AtomicHashArrayTest.cpp
      TEST atomic_hash_map_test HANGING
        SOURCES AtomicHashMapTest.cpp
      TEST atomic_linked_list_test SOURCES AtomicLinkedListTest.cpp
      TEST atomic_unordered_map_test SOURCES AtomicUnorderedMapTest.cpp
      TEST base64_test SOURCES base64_test.cpp
      TEST clock_gettime_wrappers_test SOURCES ClockGettimeWrappersTest.cpp
      TEST concurrent_bit_set_test SOURCES ConcurrentBitSetTest.cpp
      BENCHMARK concurrent_skip_list_benchmark
        SOURCES ConcurrentSkipListBenchmark.cpp
      TEST concurrent_skip_list_test SOURCES ConcurrentSkipListTest.cpp
      TEST constexpr_math_test SOURCES ConstexprMathTest.cpp
      TEST conv_test WINDOWS_DISABLED SOURCES ConvTest.cpp
      TEST cpu_id_test SOURCES CpuIdTest.cpp
      TEST demangle_test SOURCES DemangleTest.cpp
      TEST deterministic_schedule_test SOURCES DeterministicScheduleTest.cpp
      TEST discriminated_ptr_test SOURCES DiscriminatedPtrTest.cpp
      TEST dynamic_test SOURCES DynamicTest.cpp
      TEST dynamic_converter_test SOURCES DynamicConverterTest.cpp
      TEST dynamic_other_test SOURCES DynamicOtherTest.cpp
      TEST endian_test SOURCES EndianTest.cpp
      TEST exception_test SOURCES ExceptionTest.cpp
      BENCHMARK exception_wrapper_benchmark WINDOWS_DISABLED
        SOURCES ExceptionWrapperBenchmark.cpp
      TEST exception_wrapper_test WINDOWS_DISABLED
        SOURCES ExceptionWrapperTest.cpp
      TEST expected_test SOURCES ExpectedTest.cpp
      BENCHMARK fbstring_benchmark WINDOWS_DISABLED
        SOURCES FBStringBenchmark.cpp
        HEADERS FBStringTestBenchmarks.cpp.h
      TEST fbstring_test WINDOWS_DISABLED SOURCES FBStringTest.cpp
      BENCHMARK fbvector_benchmark
        SOURCES FBVectorBenchmark.cpp
        HEADERS FBVectorBenchmarks.cpp.h
      TEST fbvector_test SOURCES FBVectorTest.cpp
      TEST file_test SOURCES FileTest.cpp
      # Open-source linux build can't handle running this.
      #TEST file_lock_test SOURCES FileLockTest.cpp
      TEST file_util_test SOURCES FileUtilTest.cpp
      BENCHMARK fingerprint_benchmark SOURCES FingerprintBenchmark.cpp
      TEST fingerprint_test SOURCES FingerprintTest.cpp
      TEST fixed_string_test SOURCES FixedStringTest.cpp
      TEST format_other_test SOURCES FormatOtherTest.cpp
      BENCHMARK format_benchmark SOURCES FormatBenchmark.cpp
      TEST format_test SOURCES FormatTest.cpp
      TEST function_test BROKEN
        SOURCES FunctionTest.cpp
      BENCHMARK function_ref_benchmark SOURCES FunctionRefBenchmark.cpp
      TEST function_ref_test SOURCES FunctionRefTest.cpp
      TEST futex_test SOURCES FutexTest.cpp
      TEST glog_test SOURCES GLogTest.cpp
      TEST group_varint_test SOURCES GroupVarintTest.cpp
      TEST group_varint_test_ssse3 SOURCES GroupVarintTest.cpp
      TEST iterators_test SOURCES IteratorsTest.cpp
      TEST indestructible_test SOURCES IndestructibleTest.cpp
      TEST indexed_mem_pool_test BROKEN
        SOURCES IndexedMemPoolTest.cpp
      # MSVC Preprocessor stringizing raw string literals bug
      TEST json_test WINDOWS_DISABLED SOURCES JsonTest.cpp
      BENCHMARK json_benchmark SOURCES JsonBenchmark.cpp
      TEST json_pointer_test SOURCES json_pointer_test.cpp
      TEST json_patch_test SOURCES json_patch_test.cpp
      TEST json_other_test SOURCES JsonOtherTest.cpp
      TEST lazy_test SOURCES LazyTest.cpp
      TEST locks_test SOURCES SpinLockTest.cpp
      TEST math_test SOURCES MathTest.cpp
      TEST map_util_test WINDOWS_DISABLED SOURCES MapUtilTest.cpp
      TEST memcpy_test SOURCES MemcpyTest.cpp
      TEST memory_idler_test SOURCES MemoryIdlerTest.cpp
      TEST memory_test WINDOWS_DISABLED
        SOURCES MemoryTest.cpp
      TEST move_wrapper_test SOURCES MoveWrapperTest.cpp
      TEST mpmc_pipeline_test SOURCES MPMCPipelineTest.cpp
      TEST mpmc_queue_test SLOW
        SOURCES MPMCQueueTest.cpp
      TEST network_address_test HANGING
        SOURCES
          IPAddressTest.cpp
          MacAddressTest.cpp
          SocketAddressTest.cpp
      TEST optional_test SOURCES OptionalTest.cpp
      TEST packed_sync_ptr_test HANGING
        SOURCES PackedSyncPtrTest.cpp
      TEST padded_test SOURCES PaddedTest.cpp
      #TEST poly_test SOURCES PolyTest.cpp
      TEST portability_test SOURCES PortabilityTest.cpp
      # Turns out this benchmark uses Linux only API for setting CPU affinity i.e. cpu_set_t.
      BENCHMARK producer_consumer_queue_benchmark APPLE_DISABLED SOURCES ProducerConsumerQueueBenchmark.cpp
      TEST producer_consumer_queue_test SLOW
        SOURCES ProducerConsumerQueueTest.cpp
      BENCHMARK range_find_benchmark SOURCES RangeFindBenchmark.cpp
      TEST random_test SOURCES RandomTest.cpp
      TEST range_test SOURCES RangeTest.cpp
      TEST replaceable_test WINDOWS_DISABLED SOURCES ReplaceableTest.cpp
      TEST scope_guard_test WINDOWS_DISABLED SOURCES ScopeGuardTest.cpp
      # Heavily dependent on drand and srand48
      #TEST shared_mutex_test SOURCES SharedMutexTest.cpp
      # SingletonTest requires Subprocess
      #TEST singleton_test SOURCES SingletonTest.cpp
      TEST singleton_test_global SOURCES SingletonTestGlobal.cpp
      TEST small_vector_test WINDOWS_DISABLED
        SOURCES small_vector_test.cpp
      TEST sorted_vector_types_test SOURCES sorted_vector_test.cpp
      BENCHMARK string_benchmark WINDOWS_DISABLED SOURCES StringBenchmark.cpp
      TEST string_test WINDOWS_DISABLED SOURCES StringTest.cpp
      BENCHMARK synchronized_benchmark WINDOWS_DISABLED
        SOURCES SynchronizedBenchmark.cpp
      TEST synchronized_test WINDOWS_DISABLED
        SOURCES SynchronizedTest.cpp
      TEST thread_cached_int_test WINDOWS_DISABLED
        SOURCES ThreadCachedIntTest.cpp
      TEST thread_local_test WINDOWS_DISABLED SOURCES ThreadLocalTest.cpp
      TEST timeout_queue_test SOURCES TimeoutQueueTest.cpp
      TEST token_bucket_test SOURCES TokenBucketTest.cpp
      TEST traits_test SOURCES TraitsTest.cpp
      TEST try_test WINDOWS_DISABLED SOURCES TryTest.cpp
      TEST unit_test SOURCES UnitTest.cpp
      BENCHMARK uri_benchmark SOURCES UriBenchmark.cpp
      TEST uri_test SOURCES UriTest.cpp
      TEST varint_test SOURCES VarintTest.cpp
  )

  if (${LIBSODIUM_FOUND})
    folly_define_tests(
      DIRECTORY experimental/crypto/test/
        TEST blake2xb_test SOURCES Blake2xbTest.cpp
        TEST lt_hash_test SOURCES LtHashTest.cpp
    )
  endif()

  if (${LIBAIO_FOUND})
    folly_define_tests(
      DIRECTORY experimental/io/test/
        TEST async_io_test
          SOURCES
            AsyncIOTest.cpp
            AsyncBaseTestLib.cpp
            IoTestTempFileUtil.cpp
    )
  endif()

  get_target_property(pic folly POSITION_INDEPENDENT_CODE)
  if (pic)
    add_library(singleton_thread_local_overload
      SHARED ${FOLLY_DIR}/test/SingletonThreadLocalTestOverload.cpp)
    apply_folly_compile_options_to_target(singleton_thread_local_overload)
    set_target_properties(singleton_thread_local_overload PROPERTIES PREFIX "")
    target_link_libraries(singleton_thread_local_overload PRIVATE folly)
    folly_define_tests(
      DIRECTORY test/
        TEST singleton_thread_local_test SOURCES SingletonThreadLocalTest.cpp
    )
  endif()
endif()

add_subdirectory(folly)
