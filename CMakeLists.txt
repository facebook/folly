cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)
# We use the GoogleTest module if it is available (only in CMake 3.9+)
# It requires CMP0054 and CMP0057 to be enabled.
if (POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()
if (POLICY CMP0057)
  cmake_policy(SET CMP0057 NEW)
endif()

# includes
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# package information
set(PACKAGE_NAME      "folly")
set(PACKAGE_VERSION   "0.58.0-dev")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/facebook/folly/issues")

# 150+ tests in the root folder anyone? No? I didn't think so.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(${PACKAGE_NAME} CXX C)

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

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Check target architecture
  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Folly requires a 64bit target architecture.")
  endif()

  if (MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1920)
    set(MSVC_IS_2017 ON)
  elseif (MSVC_VERSION EQUAL 1900)
    set(MSVC_IS_2017 OFF)
  else()
    message(
      FATAL_ERROR
      "This build script only supports building Folly on 64-bit Windows with "
      "Visual Studio 2015 or Visual Studio 2017. "
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

include(folly-deps) # Find the required packages

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  include(FollyCompilerMSVC)
else()
  include(FollyCompilerUnix)
endif()
include(FollyFunctions)

include(FollyConfigChecks)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/folly-config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
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
    "/build/"
    "/experimental/exception_tracer/"
    "/experimental/hazptr/bench/"
    "/experimental/hazptr/example/"
    "/experimental/logging/example/"
    "/futures/exercises/"
    "/test/"
    "/tools/"
    "Benchmark.cpp$"
    "Test.cpp$"
)
list(REMOVE_ITEM files
  ${FOLLY_DIR}/experimental/JSONSchemaTester.cpp
  ${FOLLY_DIR}/experimental/io/HugePageUtil.cpp
  ${FOLLY_DIR}/experimental/symbolizer/ElfUtil.cpp
)
list(REMOVE_ITEM hfiles
  ${FOLLY_DIR}/detail/SlowFingerprint.h
  ${FOLLY_DIR}/detail/FingerprintPolynomial.h
)

# Explicitly include utility library code from inside
# folly/test and folly/io/async/test/
list(APPEND files
  ${FOLLY_DIR}/io/async/test/ScopedBoundPort.cpp
  ${FOLLY_DIR}/io/async/test/SocketPair.cpp
  ${FOLLY_DIR}/io/async/test/TimeUtil.cpp
)
list(APPEND hfiles
  ${FOLLY_DIR}/io/async/test/AsyncSSLSocketTest.h
  ${FOLLY_DIR}/io/async/test/AsyncSocketTest2.h
  ${FOLLY_DIR}/io/async/test/BlockingSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncServerSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncSSLSocket.h
  ${FOLLY_DIR}/io/async/test/MockAsyncTransport.h
  ${FOLLY_DIR}/io/async/test/MockAsyncUDPSocket.h
  ${FOLLY_DIR}/io/async/test/MockTimeoutManager.h
  ${FOLLY_DIR}/io/async/test/ScopedBoundPort.h
  ${FOLLY_DIR}/io/async/test/SocketPair.h
  ${FOLLY_DIR}/io/async/test/TestSSLServer.h
  ${FOLLY_DIR}/io/async/test/TimeUtil.h
  ${FOLLY_DIR}/io/async/test/UndelayedDestruction.h
  ${FOLLY_DIR}/io/async/test/Util.h
  ${FOLLY_DIR}/test/TestUtils.h
)

# Exclude specific sources if we do not have third-party libraries
# required to build them.
if (NOT FOLLY_USE_SYMBOLIZER)
  REMOVE_MATCHES_FROM_LISTS(files hfiles
    MATCHES
      "/experimental/symbolizer/"
  )
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/SingletonStackTrace.cpp
  )
endif()
if (NOT ${LIBURCU_FOUND})
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/experimental/RCUUtils.cpp
  )
  list(REMOVE_ITEM hfiles
    ${FOLLY_DIR}/experimental/RCURefCount.h
    ${FOLLY_DIR}/experimental/RCUUtils.h
  )
endif()
if (NOT ${LIBAIO_FOUND})
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/experimental/io/AsyncIO.cpp
  )
  list(REMOVE_ITEM hfiles
    ${FOLLY_DIR}/experimental/io/AsyncIO.h
  )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(REMOVE_ITEM files
    ${FOLLY_DIR}/Poly.cpp
    ${FOLLY_DIR}/Subprocess.cpp
  )
  list(REMOVE_ITEM hfiles
    ${FOLLY_DIR}/Poly.h
    ${FOLLY_DIR}/Poly-inl.h
    ${FOLLY_DIR}/detail/PolyDetail.h
    ${FOLLY_DIR}/detail/TypeList.h
    ${FOLLY_DIR}/poly/Nullable.h
    ${FOLLY_DIR}/poly/Regular.h
  )
endif()

add_library(folly_base OBJECT
  ${files} ${hfiles}
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
)
if (BUILD_SHARED_LIBS)
  set_property(TARGET folly_base PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()
auto_source_group(folly ${FOLLY_DIR} ${files} ${hfiles})
apply_folly_compile_options_to_target(folly_base)
target_include_directories(folly_base PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
# Add the generated files to the correct source group.
source_group("folly" FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h)
source_group("folly\\build" FILES
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(FOLLY_LINK_LIBRARIES
    ${FOLLY_LINK_LIBRARIES}
    Iphlpapi.lib
    Ws2_32.lib
  )
endif()

set(FOLLY_LINK_LIBRARIES
  ${FOLLY_LINK_LIBRARIES}
  ${FOLLY_SHINY_DEPENDENCIES}
)

target_include_directories(folly_base
  PUBLIC
    ${FOLLY_INCLUDE_DIRECTORIES}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

foreach (LIB ${FOLLY_SHINY_DEPENDENCIES})
  target_include_directories(folly_base PUBLIC $<TARGET_PROPERTY:${LIB},INCLUDE_DIRECTORIES>)
  target_compile_definitions(folly_base PUBLIC $<TARGET_PROPERTY:${LIB},INTERFACE_COMPILE_DEFINITIONS>)
endforeach()

if (FOLLY_HAVE_PTHREAD)
  target_include_directories(folly_base PUBLIC ${LIBPTHREAD_INCLUDE_DIRS})
endif()

# Now to generate the fingerprint tables
add_executable(GenerateFingerprintTables
  ${FOLLY_DIR}/build/GenerateFingerprintTables.cpp
  $<TARGET_OBJECTS:folly_base>
)
target_link_libraries(GenerateFingerprintTables PRIVATE ${FOLLY_LINK_LIBRARIES})
target_include_directories(GenerateFingerprintTables PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
add_dependencies(GenerateFingerprintTables folly_base)
apply_folly_compile_options_to_target(GenerateFingerprintTables)
set_property(TARGET GenerateFingerprintTables PROPERTY FOLDER "Build")
source_group("" FILES ${FOLLY_DIR}/build/GenerateFingerprintTables.cpp)

# Compile the fingerprint tables.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
  COMMAND
    ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/folly/build
  COMMAND
    GenerateFingerprintTables
    --install_dir ${CMAKE_CURRENT_BINARY_DIR}/folly/build
  DEPENDS GenerateFingerprintTables
  COMMENT "Generating the fingerprint tables..."
)
add_library(folly_fingerprint STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
  ${FOLLY_DIR}/Fingerprint.h
  ${FOLLY_DIR}/detail/SlowFingerprint.h
  ${FOLLY_DIR}/detail/FingerprintPolynomial.h
  $<TARGET_OBJECTS:folly_base>
)
target_link_libraries(folly_fingerprint PRIVATE ${FOLLY_LINK_LIBRARIES})
target_include_directories(folly_fingerprint PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
add_dependencies(folly_fingerprint folly_base)
apply_folly_compile_options_to_target(folly_fingerprint)

# We want to generate a single library and target for folly, but we needed a
# two-stage compile for the fingerprint tables, so we create a phony source
# file that we modify whenever the base libraries change, causing folly to be
# re-linked, making things happy.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp
  DEPENDS folly_base folly_fingerprint
)
add_library(folly
  ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp
  $<TARGET_OBJECTS:folly_base>
)
apply_folly_compile_options_to_target(folly)
source_group("" FILES ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp)

target_link_libraries(folly PUBLIC ${FOLLY_LINK_LIBRARIES})
target_include_directories(folly
  INTERFACE
    ${FOLLY_INCLUDE_DIRECTORIES}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS folly
  EXPORT folly
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
auto_install_files(folly ${FOLLY_DIR}
  ${hfiles}
  ${FOLLY_DIR}/Fingerprint.h
  ${FOLLY_DIR}/detail/SlowFingerprint.h
  ${FOLLY_DIR}/detail/FingerprintPolynomial.h
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
  DESTINATION include/folly
  COMPONENT dev
)

# Generate the folly-config.cmake file for installation so that
# downstream projects that use on folly can easily depend on it in their CMake
# files using "find_package(folly CONFIG)"
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/folly-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/folly-config.cmake
  @ONLY
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/folly-config.cmake
  DESTINATION share/folly
  COMPONENT dev
)
install(
  EXPORT folly
  DESTINATION share/folly
  NAMESPACE Folly::
  FILE folly-targets.cmake
  COMPONENT dev
)

option(BUILD_TESTS "If enabled, compile the tests." OFF)
option(BUILD_BROKEN_TESTS "If enabled, compile tests that are known to be broken." OFF)
option(BUILD_HANGING_TESTS "If enabled, compile tests that are known to hang." OFF)
option(BUILD_SLOW_TESTS "If enabled, compile tests that take a while to run in debug mode." OFF)
if (BUILD_TESTS)
  find_package(GMock MODULE REQUIRED)
  include(GoogleTest OPTIONAL RESULT_VARIABLE HAVE_CMAKE_GTEST)
  enable_testing()

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
    ${FOLLY_DIR}/test/DeterministicSchedule.cpp
    ${FOLLY_DIR}/test/DeterministicSchedule.h
    ${FOLLY_DIR}/test/SingletonTestStructs.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.h
    ${FOLLY_DIR}/experimental/logging/test/ConfigHelpers.cpp
    ${FOLLY_DIR}/experimental/logging/test/ConfigHelpers.h
    ${FOLLY_DIR}/experimental/logging/test/TestLogHandler.cpp
    ${FOLLY_DIR}/experimental/logging/test/TestLogHandler.h
    ${FOLLY_DIR}/futures/test/TestExecutor.cpp
    ${FOLLY_DIR}/futures/test/TestExecutor.h
    ${FOLLY_DIR}/io/async/test/BlockingSocket.h
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
    ${FOLLY_DIR}/io/async/test/TestSSLServer.cpp
    ${FOLLY_DIR}/io/async/test/TestSSLServer.h
    ${FOLLY_DIR}/io/async/test/TimeUtil.cpp
    ${FOLLY_DIR}/io/async/test/TimeUtil.h
    ${FOLLY_DIR}/io/async/test/UndelayedDestruction.h
    ${FOLLY_DIR}/io/async/test/Util.h
  )
  target_compile_definitions(folly_test_support
    PUBLIC
      ${LIBGMOCK_DEFINES}
  )
  target_include_directories(folly_test_support
    SYSTEM
    PUBLIC
      ${LIBGMOCK_INCLUDE_DIR}
  )
  target_link_libraries(folly_test_support
    PUBLIC
      ${BOOST_LIBRARIES}
      follybenchmark
      folly
      ${LIBGMOCK_LIBRARIES}
  )
  apply_folly_compile_options_to_target(folly_test_support)

  folly_define_tests(
    DIRECTORY chrono/test/
      TEST chrono_conv_test SOURCES ConvTest.cpp

    DIRECTORY compression/test/
      TEST compression_test SLOW SOURCES CompressionTest.cpp

    DIRECTORY container/test/
      TEST access_test SOURCES AccessTest.cpp
      TEST array_test SOURCES ArrayTest.cpp
      TEST bit_iterator_test SOURCES BitIteratorTest.cpp
      # TODO: CMake's gtest_add_tests() function currently chokes on
      # EnumerateTest.cpp since it uses macros to define tests.
      #TEST enumerate_test SOURCES EnumerateTest.cpp
      TEST evicting_cache_map_test SOURCES EvictingCacheMapTest.cpp
      TEST f14_pre_test SOURCES F14PreTest.cpp
      TEST f14_map_test SOURCES F14MapTest.cpp
      TEST f14_set_test SOURCES F14SetTest.cpp
      TEST foreach_test SOURCES ForeachTest.cpp
      TEST merge_test SOURCES MergeTest.cpp
      TEST sparse_byte_set_test SOURCES SparseByteSetTest.cpp

    DIRECTORY concurrency/test/
      TEST cache_locality_test SOURCES CacheLocalityTest.cpp

    DIRECTORY executors/test/
      TEST async_helpers_test SOURCES AsyncTest.cpp
      TEST codel_test SOURCES CodelTest.cpp
      TEST executor_test SOURCES ExecutorTest.cpp
      TEST fiber_io_executor_test SOURCES FiberIOExecutorTest.cpp
      TEST global_executor_test SOURCES GlobalExecutorTest.cpp
      TEST serial_executor_test SOURCES SerialExecutorTest.cpp
      TEST thread_pool_executor_test SOURCES ThreadPoolExecutorTest.cpp
      TEST threaded_executor_test SOURCES ThreadedExecutorTest.cpp
      TEST timed_drivable_executor_test SOURCES TimedDrivableExecutorTest.cpp

    DIRECTORY executors/task_queue/test/
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
      # Depends on liburcu
      #TEST read_mostly_shared_ptr_test SOURCES ReadMostlySharedPtrTest.cpp
      #TEST ref_count_test SOURCES RefCountTest.cpp
      TEST stringkeyed_test SOURCES StringKeyedTest.cpp
      TEST test_util_test SOURCES TestUtilTest.cpp
      TEST tuple_ops_test SOURCES TupleOpsTest.cpp

    DIRECTORY experimental/io/test/
      # Depends on libaio
      #TEST async_io_test SOURCES AsyncIOTest.cpp
      TEST fs_util_test SOURCES FsUtilTest.cpp

    DIRECTORY experimental/logging/test/
      TEST async_file_writer_test SOURCES AsyncFileWriterTest.cpp
      TEST config_parser_test SOURCES ConfigParserTest.cpp
      TEST config_update_test SOURCES ConfigUpdateTest.cpp
      TEST file_handler_factory_test SOURCES FileHandlerFactoryTest.cpp
      TEST glog_formatter_test SOURCES GlogFormatterTest.cpp
      TEST immediate_file_writer_test SOURCES ImmediateFileWriterTest.cpp
      TEST log_category_test SOURCES LogCategoryTest.cpp
      TEST logger_db_test SOURCES LoggerDBTest.cpp
      TEST logger_test SOURCES LoggerTest.cpp
      TEST log_level_test SOURCES LogLevelTest.cpp
      TEST log_message_test SOURCES LogMessageTest.cpp
      TEST log_name_test SOURCES LogNameTest.cpp
      TEST log_stream_test SOURCES LogStreamTest.cpp
      TEST printf_test SOURCES PrintfTest.cpp
      TEST rate_limiter_test SOURCES RateLimiterTest.cpp
      TEST standard_log_handler_test SOURCES StandardLogHandlerTest.cpp
      TEST xlog_test
        HEADERS
          XlogHeader1.h
          XlogHeader2.h
        SOURCES
          XlogFile1.cpp
          XlogFile2.cpp
          XlogTest.cpp

    DIRECTORY fibers/test/
      TEST fibers_test SOURCES FibersTest.cpp

    DIRECTORY functional/test/
      TEST apply_tuple_test SOURCES ApplyTupleTest.cpp
      TEST partial_test SOURCES PartialTest.cpp

    DIRECTORY futures/test/
      TEST barrier_test SOURCES BarrierTest.cpp
      TEST callback_lifetime_test SOURCES CallbackLifetimeTest.cpp
      TEST collect_test SOURCES CollectTest.cpp
      TEST context_test SOURCES ContextTest.cpp
      TEST core_test SOURCES CoreTest.cpp
      TEST ensure_test SOURCES EnsureTest.cpp
      TEST fsm_test SOURCES FSMTest.cpp
      TEST filter_test SOURCES FilterTest.cpp
      TEST future_splitter_test SOURCES FutureSplitterTest.cpp
      # MSVC SFINAE bug
      #TEST future_test SOURCES FutureTest.cpp
      TEST header_compile_test SOURCES HeaderCompileTest.cpp
      TEST interrupt_test SOURCES InterruptTest.cpp
      TEST map_test SOURCES MapTest.cpp
      TEST non_copyable_lambda_test SOURCES NonCopyableLambdaTest.cpp
      TEST poll_test SOURCES PollTest.cpp
      TEST promise_test SOURCES PromiseTest.cpp
      TEST reduce_test SOURCES ReduceTest.cpp
      # MSVC SFINAE bug
      #TEST retrying_test SOURCES RetryingTest.cpp
      TEST self_destruct_test SOURCES SelfDestructTest.cpp
      TEST shared_promise_test SOURCES SharedPromiseTest.cpp
      TEST test_executor_test SOURCES TestExecutorTest.cpp
      TEST then_compile_test
        HEADERS
          ThenCompileTest.h
        SOURCES
          ThenCompileTest.cpp
      TEST then_test SOURCES ThenTest.cpp
      TEST timekeeper_test SOURCES TimekeeperTest.cpp
      TEST times_test SOURCES TimesTest.cpp
      TEST unwrap_test SOURCES UnwrapTest.cpp
      TEST via_test SOURCES ViaTest.cpp
      TEST wait_test SOURCES WaitTest.cpp
      TEST when_test SOURCES WhenTest.cpp
      TEST while_do_test SOURCES WhileDoTest.cpp
      TEST will_equal_test SOURCES WillEqualTest.cpp
      TEST window_test SOURCES WindowTest.cpp

    DIRECTORY gen/test/
      # MSVC bug can't resolve initializer_list constructor properly
      #TEST base_test SOURCES BaseTest.cpp
      TEST combine_test SOURCES CombineTest.cpp
      TEST parallel_map_test SOURCES ParallelMapTest.cpp
      TEST parallel_test SOURCES ParallelTest.cpp

    DIRECTORY hash/test/
      TEST checksum_test SOURCES ChecksumTest.cpp
      TEST hash_test SOURCES HashTest.cpp
      TEST spooky_hash_v1_test SOURCES SpookyHashV1Test.cpp
      TEST spooky_hash_v2_test SOURCES SpookyHashV2Test.cpp

    DIRECTORY io/test/
      TEST iobuf_test SOURCES IOBufTest.cpp
      TEST iobuf_cursor_test SOURCES IOBufCursorTest.cpp
      TEST iobuf_queue_test SOURCES IOBufQueueTest.cpp
      TEST record_io_test SOURCES RecordIOTest.cpp
      TEST ShutdownSocketSetTest HANGING
        SOURCES ShutdownSocketSetTest.cpp

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
      TEST AsyncUDPSocketTest SOURCES AsyncUDPSocketTest.cpp
      TEST DelayedDestructionTest SOURCES DelayedDestructionTest.cpp
      TEST DelayedDestructionBaseTest SOURCES DelayedDestructionBaseTest.cpp
      TEST DestructorCheckTest SOURCES DestructorCheckTest.cpp
      TEST EventBaseTest SOURCES EventBaseTest.cpp
      TEST EventBaseLocalTest SOURCES EventBaseLocalTest.cpp
      TEST HHWheelTimerTest SOURCES HHWheelTimerTest.cpp
      TEST HHWheelTimerSlowTests SLOW
        SOURCES HHWheelTimerSlowTests.cpp
      TEST NotificationQueueTest SOURCES NotificationQueueTest.cpp
      TEST RequestContextTest SOURCES RequestContextTest.cpp
      TEST ScopedEventBaseThreadTest SOURCES ScopedEventBaseThreadTest.cpp
      TEST ssl_session_test
        CONTENT_DIR certs/
        SOURCES
          SSLSessionTest.cpp
      TEST writechain_test SOURCES WriteChainAsyncTransportWrapperTest.cpp

    DIRECTORY io/async/ssl/test/
      TEST ssl_errors_test SOURCES SSLErrorsTest.cpp

    DIRECTORY lang/test/
      TEST bits_test SOURCES BitsTest.cpp
      TEST cold_class_test SOURCES ColdClassTest.cpp
      TEST safe_assert_test SOURCES SafeAssertTest.cpp

    DIRECTORY memory/test/
      TEST arena_test SOURCES ArenaTest.cpp
      TEST thread_cached_arena_test SOURCES ThreadCachedArenaTest.cpp
      TEST mallctl_helper_test SOURCES MallctlHelperTest.cpp

    DIRECTORY portability/test/
      TEST constexpr_test SOURCES ConstexprTest.cpp
      TEST libgen-test SOURCES LibgenTest.cpp
      TEST openssl_portability_test SOURCES OpenSSLPortabilityTest.cpp
      TEST time-test SOURCES TimeTest.cpp

    DIRECTORY ssl/test/
      TEST openssl_hash_test SOURCES OpenSSLHashTest.cpp

    DIRECTORY stats/test/
      TEST histogram_test SOURCES HistogramTest.cpp
      TEST timeseries_histogram_test SOURCES TimeseriesHistogramTest.cpp
      TEST timeseries_test SOURCES TimeSeriesTest.cpp

    DIRECTORY synchronization/test/
      TEST baton_test SOURCES BatonTest.cpp
      TEST call_once_test SOURCES CallOnceTest.cpp
      TEST lifo_sem_test SOURCES LifoSemTests.cpp
      TEST rw_spin_lock_test SOURCES RWSpinLockTest.cpp

    DIRECTORY system/test/
      TEST memory_mapping_test SOURCES MemoryMappingTest.cpp
      TEST shell_test SOURCES ShellTest.cpp
      #TEST subprocess_test SOURCES SubprocessTest.cpp
      TEST thread_id_test SOURCES ThreadIdTest.cpp
      TEST thread_name_test SOURCES ThreadNameTest.cpp

    DIRECTORY synchronization/test/
      TEST atomic_struct_test SOURCES AtomicStructTest.cpp
      TEST small_locks_test SOURCES SmallLocksTest.cpp

    DIRECTORY test/
      TEST ahm_int_stress_test SOURCES AHMIntStressTest.cpp
      TEST arena_smartptr_test SOURCES ArenaSmartPtrTest.cpp
      TEST ascii_check_test SOURCES AsciiCaseInsensitiveTest.cpp
      TEST atomic_bit_set_test SOURCES AtomicBitSetTest.cpp
      TEST atomic_hash_array_test SOURCES AtomicHashArrayTest.cpp
      TEST atomic_hash_map_test HANGING
        SOURCES AtomicHashMapTest.cpp
      TEST atomic_linked_list_test SOURCES AtomicLinkedListTest.cpp
      TEST atomic_unordered_map_test SOURCES AtomicUnorderedMapTest.cpp
      TEST cacheline_padded_test SOURCES CachelinePaddedTest.cpp
      TEST clock_gettime_wrappers_test SOURCES ClockGettimeWrappersTest.cpp
      TEST concurrent_skip_list_test SOURCES ConcurrentSkipListTest.cpp
      TEST conv_test SOURCES ConvTest.cpp
      TEST cpu_id_test SOURCES CpuIdTest.cpp
      TEST demangle_test SOURCES DemangleTest.cpp
      TEST deterministic_schedule_test SOURCES DeterministicScheduleTest.cpp
      TEST discriminated_ptr_test SOURCES DiscriminatedPtrTest.cpp
      TEST dynamic_test SOURCES DynamicTest.cpp
      TEST dynamic_converter_test SOURCES DynamicConverterTest.cpp
      TEST dynamic_other_test SOURCES DynamicOtherTest.cpp
      TEST endian_test SOURCES EndianTest.cpp
      TEST exception_test SOURCES ExceptionTest.cpp
      TEST exception_wrapper_test SOURCES ExceptionWrapperTest.cpp
      TEST expected_test SOURCES ExpectedTest.cpp
      TEST fbvector_test SOURCES FBVectorTest.cpp
      TEST file_test SOURCES FileTest.cpp
      #TEST file_lock_test SOURCES FileLockTest.cpp
      TEST file_util_test HANGING
        SOURCES FileUtilTest.cpp
      # TEST fingerprint_test SOURCES FingerprintTest.cpp
      TEST format_other_test SOURCES FormatOtherTest.cpp
      TEST format_test SOURCES FormatTest.cpp
      TEST function_test BROKEN
        SOURCES FunctionTest.cpp
      TEST function_ref_test SOURCES FunctionRefTest.cpp
      TEST futex_test SOURCES FutexTest.cpp
      TEST group_varint_test SOURCES GroupVarintTest.cpp
      TEST group_varint_test_ssse3 SOURCES GroupVarintTest.cpp
      TEST has_member_fn_traits_test SOURCES HasMemberFnTraitsTest.cpp
      TEST indestructible_test SOURCES IndestructibleTest.cpp
      TEST indexed_mem_pool_test BROKEN
        SOURCES IndexedMemPoolTest.cpp
      # MSVC Preprocessor stringizing raw string literals bug
      #TEST json_test SOURCES JsonTest.cpp
      TEST json_pointer_test SOURCES json_pointer_test.cpp
      TEST json_other_test
        CONTENT_DIR json_test_data/
        SOURCES
          JsonOtherTest.cpp
      TEST lazy_test SOURCES LazyTest.cpp
      TEST lock_traits_test SOURCES LockTraitsTest.cpp
      TEST locks_test SOURCES SpinLockTest.cpp
      TEST logging_test SOURCES LoggingTest.cpp
      TEST math_test SOURCES MathTest.cpp
      TEST map_util_test SOURCES MapUtilTest.cpp
      TEST memcpy_test SOURCES MemcpyTest.cpp
      TEST memory_idler_test SOURCES MemoryIdlerTest.cpp
      TEST memory_test SOURCES MemoryTest.cpp
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
      TEST producer_consumer_queue_test SLOW
        SOURCES ProducerConsumerQueueTest.cpp
      TEST random_test SOURCES RandomTest.cpp
      TEST range_test SOURCES RangeTest.cpp
      TEST scope_guard_test SOURCES ScopeGuardTest.cpp
      # Heavily dependent on drand and srand48
      #TEST shared_mutex_test SOURCES SharedMutexTest.cpp
      # SingletonTest requires Subprocess
      #TEST singleton_test SOURCES SingletonTest.cpp
      TEST singleton_test_global SOURCES SingletonTestGlobal.cpp
      TEST singleton_thread_local_test SOURCES SingletonThreadLocalTest.cpp
      TEST small_vector_test SOURCES small_vector_test.cpp
      TEST sorted_vector_types_test SOURCES sorted_vector_test.cpp
      TEST string_test SOURCES StringTest.cpp
      TEST synchronized_test SOURCES SynchronizedTest.cpp
      TEST thread_cached_int_test SOURCES ThreadCachedIntTest.cpp
      TEST thread_local_test SOURCES ThreadLocalTest.cpp
      TEST timeout_queue_test SOURCES TimeoutQueueTest.cpp
      TEST token_bucket_test SOURCES TokenBucketTest.cpp
      TEST traits_test SOURCES TraitsTest.cpp
      TEST try_test SOURCES TryTest.cpp
      TEST unit_test SOURCES UnitTest.cpp
      TEST uri_test SOURCES UriTest.cpp
      TEST varint_test SOURCES VarintTest.cpp
  )
endif()

add_subdirectory(folly)
