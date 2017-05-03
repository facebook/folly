cmake_minimum_required(VERSION 3.4.0 FATAL_ERROR)

# Unfortunately, CMake doesn't easily provide us a way to merge static
# libraries, which is what we want to do to generate the main folly library, so
# we do a bit of a workaround here to inject a property into the generated
# project files that will only get enabled for the folly target. Ugly, but
# the alternatives are far, far worse.
if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio 15( 2017)? Win64")
  set(CMAKE_GENERATOR_TOOLSET "v141</PlatformToolset></PropertyGroup><ItemDefinitionGroup Condition=\"'$(ProjectName)'=='folly'\"><ProjectReference><LinkLibraryDependencies>true</LinkLibraryDependencies></ProjectReference></ItemDefinitionGroup><PropertyGroup><PlatformToolset>v141")
  set(MSVC_IS_2017 ON)
elseif ("${CMAKE_GENERATOR}" STREQUAL "Visual Studio 14 2015 Win64")
  set(CMAKE_GENERATOR_TOOLSET "v140</PlatformToolset></PropertyGroup><ItemDefinitionGroup Condition=\"'$(ProjectName)'=='folly'\"><ProjectReference><LinkLibraryDependencies>true</LinkLibraryDependencies></ProjectReference></ItemDefinitionGroup><PropertyGroup><PlatformToolset>v140")
  set(MSVC_IS_2017 OFF)
else()
  message(FATAL_ERROR "This build script only supports building Folly on 64-bit Windows with Visual Studio 2015 or Visual Studio 2017.")
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

project(${PACKAGE_NAME} CXX)

# Check architecture OS
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Folly requires a 64bit OS")
endif()
if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  message(FATAL_ERROR "You should only be using CMake to build Folly if you are on Windows!")
endif()

set(FOLLY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/folly")

# Generate a few tables and create the main config file.
find_package(PythonInterp REQUIRED)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly/build/EscapeTables.cpp
  COMMAND ${PYTHON_EXECUTABLE} "${FOLLY_DIR}/build/generate_escape_tables.py"
  DEPENDS ${FOLLY_DIR}/build/generate_escape_tables.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/folly/build/
  COMMENT "Generating the escape tables..." VERBATIM
)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FormatTables.cpp
  COMMAND ${PYTHON_EXECUTABLE} "${FOLLY_DIR}/build/generate_format_tables.py"
  DEPENDS ${FOLLY_DIR}/build/generate_format_tables.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/folly/build/
  COMMENT "Generating the format tables..." VERBATIM
)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/folly/build/GroupVarintTables.cpp"
  COMMAND ${PYTHON_EXECUTABLE} "${FOLLY_DIR}/build/generate_varint_tables.py"
  DEPENDS ${FOLLY_DIR}/build/generate_varint_tables.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/folly/build/
  COMMENT "Generating the group varint tables..." VERBATIM
)

include(folly-deps) # Find the required packages
if (LIBPTHREAD_FOUND)
  set(FOLLY_HAVE_PTHREAD ON)
endif()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/folly-config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
)

include(FollyCompiler)
include(FollyFunctions)

# Main folly library files
auto_sources(files "*.cpp" "RECURSE" "${FOLLY_DIR}")
auto_sources(hfiles "*.h" "RECURSE" "${FOLLY_DIR}")

# No need for tests or benchmarks, and we can't build most experimental stuff.
REMOVE_MATCHES_FROM_LISTS(files hfiles
  MATCHES
    "/build/"
    "/experimental/exception_tracer/"
    "/experimental/hazptr/example/"
    "/experimental/symbolizer/"
    "/futures/exercises/"
    "/test/"
    "Benchmark.cpp$"
    "Test.cpp$"
  IGNORE_MATCHES
    "/Benchmark.cpp$"
)
list(REMOVE_ITEM files
  ${FOLLY_DIR}/Subprocess.cpp
  ${FOLLY_DIR}/SingletonStackTrace.cpp
  ${FOLLY_DIR}/experimental/JSONSchemaTester.cpp
  ${FOLLY_DIR}/experimental/RCUUtils.cpp
  ${FOLLY_DIR}/experimental/io/AsyncIO.cpp
  ${FOLLY_DIR}/experimental/io/HugePageUtil.cpp
  ${FOLLY_DIR}/futures/test/Benchmark.cpp
)
list(REMOVE_ITEM hfiles
  ${FOLLY_DIR}/Fingerprint.h
  ${FOLLY_DIR}/detail/SlowFingerprint.h
  ${FOLLY_DIR}/detail/FingerprintPolynomial.h
  ${FOLLY_DIR}/experimental/RCURefCount.h
  ${FOLLY_DIR}/experimental/RCUUtils.h
  ${FOLLY_DIR}/experimental/io/AsyncIO.h
)

add_library(folly_base STATIC
  ${files} ${hfiles}
  ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/EscapeTables.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FormatTables.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/GroupVarintTables.cpp
)
auto_source_group(folly ${FOLLY_DIR} ${files} ${hfiles})
apply_folly_compile_options_to_target(folly_base)
target_include_directories(folly_base PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
# Add the generated files to the correct source group.
source_group("folly" FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h)
source_group("folly\\build" FILES
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/EscapeTables.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FormatTables.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/GroupVarintTables.cpp
)

target_include_directories(folly_base
  PUBLIC
    ${DOUBLE_CONVERSION_INCLUDE_DIR}
    ${LIBGFLAGS_INCLUDE_DIR}
    ${LIBGLOG_INCLUDE_DIR}
    ${LIBEVENT_INCLUDE_DIR}
)
target_link_libraries(folly_base
  PUBLIC
    Boost::chrono
    Boost::context
    Boost::date_time
    Boost::filesystem
    Boost::program_options
    Boost::regex
    Boost::system
    ${DOUBLE_CONVERSION_LIBRARY}
    ${LIBEVENT_LIB}
    ${LIBGFLAGS_LIBRARY}
    ${LIBGLOG_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
    Ws2_32.lib
)
if (FOLLY_HAVE_PTHREAD)
  target_include_directories(folly_base PUBLIC ${LIBPTHREAD_INCLUDE_DIRS})
  target_link_libraries(folly_base PUBLIC ${LIBPTHREAD_LIBRARIES})
endif()

# Now to generate the fingerprint tables
add_executable(GenerateFingerprintTables
  ${FOLLY_DIR}/build/GenerateFingerprintTables.cpp
)
apply_folly_compile_options_to_target(GenerateFingerprintTables)
set_property(TARGET GenerateFingerprintTables PROPERTY FOLDER "Build")
target_link_libraries(GenerateFingerprintTables PRIVATE folly_base)
source_group("" FILES ${FOLLY_DIR}/build/GenerateFingerprintTables.cpp)

# Compile the fingerprint tables.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
  COMMAND GenerateFingerprintTables
  DEPENDS GenerateFingerprintTables
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/folly/build/
  COMMENT "Generating the fingerprint tables..."
)
add_library(folly_fingerprint STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/folly/build/FingerprintTables.cpp
  ${FOLLY_DIR}/Fingerprint.h
  ${FOLLY_DIR}/detail/SlowFingerprint.h
  ${FOLLY_DIR}/detail/FingerprintPolynomial.h
)
apply_folly_compile_options_to_target(folly_fingerprint)
target_link_libraries(folly_fingerprint PRIVATE folly_base)

# We want to generate a single library and target for folly, but we needed a
# two-stage compile for the fingerprint tables, so we create a phony source
# file that we modify whenever the base libraries change, causing folly to be
# re-linked, making things happy.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp
  DEPENDS folly_base folly_fingerprint
)
add_library(folly ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp)
apply_folly_compile_options_to_target(folly)
source_group("" FILES ${CMAKE_CURRENT_BINARY_DIR}/folly_dep.cpp)

# Rather than list the dependencies in two places, we apply them directly on
# the folly_base target and then copy them over to the folly target.
get_target_property(FOLLY_LINK_LIBRARIES folly_base INTERFACE_LINK_LIBRARIES)
target_link_libraries(folly PUBLIC ${FOLLY_LINK_LIBRARIES})
target_include_directories(folly PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/folly/folly-config.h DESTINATION include/folly)
install(
  EXPORT folly
  DESTINATION share/folly
  NAMESPACE Folly::
  FILE folly-targets.cmake
)

# We need a wrapper config file to do the find_package calls to ensure
# that all our dependencies are available to link against.
file(
  COPY ${CMAKE_CURRENT_SOURCE_DIR}/CMake/folly-deps.cmake
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/
)
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/folly-deps.cmake "\ninclude(folly-targets.cmake)\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/folly-deps.cmake DESTINATION share/folly RENAME folly-config.cmake)

option(BUILD_TESTS "If enabled, compile the tests." OFF)
option(BUILD_HANGING_TESTS "If enabled, compile tests that are known to hang." OFF)
option(BUILD_SLOW_TESTS "If enabled, compile tests that take a while to run in debug mode." OFF)
if (BUILD_TESTS)
  find_package(GMock MODULE REQUIRED)

  add_library(folly_test_support
    ${FOLLY_DIR}/test/DeterministicSchedule.cpp
    ${FOLLY_DIR}/test/DeterministicSchedule.h
    ${FOLLY_DIR}/test/SingletonTestStructs.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.cpp
    ${FOLLY_DIR}/test/SocketAddressTestHelper.h
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
  target_include_directories(folly_test_support
    PUBLIC
      ${LIBGMOCK_INCLUDE_DIR}
  )
  target_link_libraries(folly_test_support
    PUBLIC
      Boost::thread
      folly
      ${LIBGMOCK_LIBRARY}
  )
  apply_folly_compile_options_to_target(folly_test_support)

  folly_define_tests(
    DIRECTORY experimental/test/
      TEST autotimer_test SOURCES AutoTimerTest.cpp
      TEST bits_test_2 SOURCES BitsTest.cpp
      TEST bitvector_test SOURCES BitVectorCodingTest.cpp
      TEST dynamic_parser_test SOURCES DynamicParserTest.cpp
      TEST eliasfano_test SOURCES EliasFanoCodingTest.cpp
      TEST event_count_test SOURCES EventCountTest.cpp
      TEST function_scheduler_test_2 SOURCES FunctionSchedulerTest.cpp
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

    DIRECTORY fibers/test/
      TEST fibers_test SOURCES FibersTest.cpp

    DIRECTORY futures/test/
      TEST futures-test
        HEADERS
          ThenCompileTest.h
        SOURCES
          BarrierTest.cpp
          CollectTest.cpp
          ContextTest.cpp
          CoreTest.cpp
          EnsureTest.cpp
          ExecutorTest.cpp
          FSMTest.cpp
          FilterTest.cpp
          # MSVC SFINAE bug
          #FutureTest.cpp
          HeaderCompileTest.cpp
          InterruptTest.cpp
          MapTest.cpp
          NonCopyableLambdaTest.cpp
          PollTest.cpp
          PromiseTest.cpp
          ReduceTest.cpp
          # MSVC SFINAE bug
          #RetryingTest.cpp
          SelfDestructTest.cpp
          SharedPromiseTest.cpp
          ThenCompileTest.cpp
          ThenTest.cpp
          TimekeeperTest.cpp
          TimesTest.cpp
          UnwrapTest.cpp
          ViaTest.cpp
          WaitTest.cpp
          WhenTest.cpp
          WhileDoTest.cpp
          WillEqualTest.cpp
          WindowTest.cpp

    DIRECTORY gen/test/
      # MSVC bug can't resolve initializer_list constructor properly
      #TEST base_test SOURCES BaseTest.cpp
      TEST combine_test SOURCES CombineTest.cpp
      TEST parallel_map_test SOURCES ParallelMapTest.cpp
      TEST parallel_test SOURCES ParallelTest.cpp

    DIRECTORY io/test/
      TEST compression_test SOURCES CompressionTest.cpp
      TEST iobuf_test SOURCES IOBufTest.cpp
      TEST iobuf_cursor_test SOURCES IOBufCursorTest.cpp
      TEST iobuf_queue_test SOURCES IOBufQueueTest.cpp
      TEST record_io_test SOURCES RecordIOTest.cpp
      TEST ShutdownSocketSetTest HANGING
        SOURCES ShutdownSocketSetTest.cpp

    DIRECTORY io/async/test/
      TEST async_test
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
      TEST async_timeout_test SOURCES AsyncTimeoutTest.cpp
      TEST AsyncUDPSocketTest SOURCES AsyncUDPSocketTest.cpp
      TEST DelayedDestructionTest SOURCES DelayedDestructionTest.cpp
      TEST DelayedDestructionBaseTest SOURCES DelayedDestructionBaseTest.cpp
      TEST EventBaseTest SOURCES EventBaseTest.cpp
      TEST EventBaseLocalTest SOURCES EventBaseLocalTest.cpp
      TEST HHWheelTimerTest SOURCES HHWheelTimerTest.cpp
      TEST HHWheelTimerSlowTests SLOW
        SOURCES HHWheelTimerSlowTests.cpp
      TEST NotificationQueueTest SOURCES NotificationQueueTest.cpp
      TEST RequestContextTest SOURCES RequestContextTest.cpp
      TEST ScopedEventBaseThreadTest SOURCES ScopedEventBaseThreadTest.cpp
      TEST ssl_session_test SOURCES SSLSessionTest.cpp
      TEST writechain_test SOURCES WriteChainAsyncTransportWrapperTest.cpp

    DIRECTORY io/async/ssl/test/
      TEST ssl_errors_test SOURCES SSLErrorsTest.cpp

    DIRECTORY portability/test/
      TEST constexpr_test SOURCES ConstexprTest.cpp
      TEST libgen-test SOURCES LibgenTest.cpp
      TEST time-test SOURCES TimeTest.cpp

    DIRECTORY ssl/test/
      TEST openssl_hash_test SOURCES OpenSSLHashTest.cpp

    DIRECTORY test/
      TEST ahm_int_stress_test SOURCES AHMIntStressTest.cpp
      TEST apply_tuple_test SOURCES ApplyTupleTest.cpp
      TEST arena_test SOURCES ArenaTest.cpp
      TEST arena_smartptr_test SOURCES ArenaSmartPtrTest.cpp
      TEST array_test SOURCES ArrayTest.cpp
      TEST ascii_check_test SOURCES AsciiCaseInsensitiveTest.cpp
      TEST atomic_bit_set_test SOURCES AtomicBitSetTest.cpp
      TEST atomic_hash_array_test SOURCES AtomicHashArrayTest.cpp
      TEST atomic_hash_map_test HANGING
        SOURCES AtomicHashMapTest.cpp
      TEST atomic_linked_list_test SOURCES AtomicLinkedListTest.cpp
      TEST atomic_struct_test SOURCES AtomicStructTest.cpp
      TEST atomic_unordered_map_test SOURCES AtomicUnorderedMapTest.cpp
      TEST baton_test SOURCES BatonTest.cpp
      TEST bit_iterator_test SOURCES BitIteratorTest.cpp
      TEST bits_test SOURCES BitsTest.cpp
      TEST cache_locality_test SOURCES CacheLocalityTest.cpp
      TEST cacheline_padded_test SOURCES CachelinePaddedTest.cpp
      TEST call_once_test SOURCES CallOnceTest.cpp
      TEST checksum_test SOURCES ChecksumTest.cpp
      TEST clock_gettime_wrappers_test SOURCES ClockGettimeWrappersTest.cpp
      TEST concurrent_skip_list_test SOURCES ConcurrentSkipListTest.cpp
      TEST container_traits_test SOURCES ContainerTraitsTest.cpp
      TEST conv_test SOURCES ConvTest.cpp
      TEST cpu_id_test SOURCES CpuIdTest.cpp
      TEST demangle_test SOURCES DemangleTest.cpp
      TEST deterministic_schedule_test SOURCES DeterministicScheduleTest.cpp
      TEST discriminated_ptr_test SOURCES DiscriminatedPtrTest.cpp
      TEST dynamic_test SOURCES DynamicTest.cpp
      TEST dynamic_converter_test SOURCES DynamicConverterTest.cpp
      TEST dynamic_other_test SOURCES DynamicOtherTest.cpp
      TEST endian_test SOURCES EndianTest.cpp
      TEST enumerate_test SOURCES EnumerateTest.cpp
      TEST evicting_cache_map_test SOURCES EvictingCacheMapTest.cpp
      TEST exception_test SOURCES ExceptionTest.cpp
      TEST exception_wrapper_test SOURCES ExceptionWrapperTest.cpp
      TEST expected_test SOURCES ExpectedTest.cpp
      TEST fbvector_test SOURCES FBVectorTest.cpp
      TEST file_test SOURCES FileTest.cpp
      #TEST file_lock_test SOURCES FileLockTest.cpp
      TEST file_util_test HANGING
        SOURCES FileUtilTest.cpp
      TEST fingerprint_test SOURCES FingerprintTest.cpp
      TEST foreach_test SOURCES ForeachTest.cpp
      TEST format_other_test SOURCES FormatOtherTest.cpp
      TEST format_test SOURCES FormatTest.cpp
      TEST function_scheduler_test SOURCES FunctionSchedulerTest.cpp
      TEST function_test SOURCES FunctionTest.cpp
      TEST function_ref_test SOURCES FunctionRefTest.cpp
      TEST futex_test SOURCES FutexTest.cpp
      TEST group_varint_test SOURCES GroupVarintTest.cpp
      TEST group_varint_test_ssse3 SOURCES GroupVarintTest.cpp
      TEST has_member_fn_traits_test SOURCES HasMemberFnTraitsTest.cpp
      TEST hash_test SOURCES HashTest.cpp
      TEST histogram_test SOURCES HistogramTest.cpp
      TEST indestructible_test SOURCES IndestructibleTest.cpp
      TEST indexed_mem_pool_test SOURCES IndexedMemPoolTest.cpp
      # MSVC Preprocessor stringizing raw string literals bug
      #TEST json_test SOURCES JsonTest.cpp
      TEST json_other_test
        CONTENT_DIR json_test_data/
        SOURCES
          JsonOtherTest.cpp
      TEST lazy_test SOURCES LazyTest.cpp
      TEST lifosem_test SOURCES LifoSemTests.cpp
      TEST lock_traits_test SOURCES LockTraitsTest.cpp
      TEST locks_test SOURCES SmallLocksTest.cpp SpinLockTest.cpp
      TEST logging_test SOURCES LoggingTest.cpp
      TEST mallctl_helper_test SOURCES MallctlHelperTest.cpp
      TEST math_test SOURCES MathTest.cpp
      TEST map_util_test SOURCES MapUtilTest.cpp
      TEST memcpy_test SOURCES MemcpyTest.cpp
      TEST memory_idler_test SOURCES MemoryIdlerTest.cpp
      TEST memory_mapping_test SOURCES MemoryMappingTest.cpp
      TEST memory_test SOURCES MemoryTest.cpp
      TEST merge SOURCES MergeTest.cpp
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
      TEST partial_test SOURCES PartialTest.cpp
      TEST portability_test SOURCES PortabilityTest.cpp
      TEST producer_consumer_queue_test SLOW
        SOURCES ProducerConsumerQueueTest.cpp
      TEST r_w_spin_lock_test SOURCES RWSpinLockTest.cpp
      TEST random_test SOURCES RandomTest.cpp
      TEST range_test SOURCES RangeTest.cpp
      TEST safe_assert_test SOURCES SafeAssertTest.cpp
      TEST scope_guard_test SOURCES ScopeGuardTest.cpp
      # Heavily dependent on drand and srand48
      #TEST shared_mutex_test SOURCES SharedMutexTest.cpp
      TEST shell_test SOURCES ShellTest.cpp
      TEST singleton_test SOURCES SingletonTest.cpp
      TEST singleton_test_global SOURCES SingletonTestGlobal.cpp
      TEST singleton_thread_local_test SOURCES SingletonThreadLocalTest.cpp
      TEST singletonvault_c_test SOURCES SingletonVaultCTest.cpp
      TEST small_vector_test SOURCES small_vector_test.cpp
      TEST sorted_vector_types_test SOURCES sorted_vector_test.cpp
      TEST sparse_byte_set_test SOURCES SparseByteSetTest.cpp
      TEST spooky_hash_v1_test SOURCES SpookyHashV1Test.cpp
      TEST spooky_hash_v2_test SOURCES SpookyHashV2Test.cpp
      TEST string_test SOURCES StringTest.cpp
      #TEST subprocess_test SOURCES SubprocessTest.cpp
      TEST synchronized_test SOURCES SynchronizedTest.cpp
      TEST thread_cached_arena_test SOURCES ThreadCachedArenaTest.cpp
      TEST thread_cached_int_test SOURCES ThreadCachedIntTest.cpp
      TEST thread_local_test SOURCES ThreadLocalTest.cpp
      TEST thread_name_test SOURCES ThreadNameTest.cpp
      TEST timeout_queue_test SOURCES TimeoutQueueTest.cpp
      TEST timeseries_histogram_test SOURCES TimeseriesHistogramTest.cpp
      TEST timeseries_test SOURCES TimeseriesTest.cpp
      TEST token_bucket_test SOURCES TokenBucketTest.cpp
      TEST traits_test SOURCES TraitsTest.cpp
      TEST try_test SOURCES TryTest.cpp
      TEST unit_test SOURCES UnitTest.cpp
      TEST uri_test SOURCES UriTest.cpp
      TEST varint_test SOURCES VarintTest.cpp
  )
endif()
