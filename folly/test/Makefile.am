SUBDIRS = . function_benchmark

ACLOCAL_AMFLAGS = -I m4

CPPFLAGS += -Igtest-1.6.0/include

TESTS= \
	sorted_vector_types_test \
	foreach_test \
	hash_test \
	timeout_queue_test \
	conv_test \
	range_test \
	bits_test \
	bit_iterator_test

lib_LTLIBRARIES = libgtestmain.la libgtest.la

libgtestmain_la_CPPFLAGS = -Igtest-1.6.0 -Igtest-1.6.0/src
libgtestmain_la_SOURCES = gtest-1.6.0/src/gtest-all.cc gtest-1.6.0/src/gtest_main.cc

libgtest_la_CPPFLAGS = -Igtest-1.6.0 -Igtest-1.6.0/src
libgtest_la_SOURCES = gtest-1.6.0/src/gtest-all.cc

noinst_HEADERS = FBStringTestBenchmarks.cpp.h \
		 FBVectorTestBenchmarks.cpp.h

noinst_PROGRAMS=benchmark_test

if HAVE_X86_64
small_locks_test_SOURCES = SmallLocksTest.cpp
small_locks_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += small_locks_test

# Fails with WARNING: Logging before InitGoogleLogging() is written to STDERR
packed_sync_ptr_test_SOURCES = PackedSyncPtrTest.cpp
packed_sync_ptr_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += packed_sync_ptr_test

small_vector_test_SOURCES = small_vector_test.cpp
small_vector_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += small_vector_test

discriminated_ptr_test_SOURCES = DiscriminatedPtrTest.cpp
discriminated_ptr_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += discriminated_ptr_test
endif

sorted_vector_types_test_SOURCES = sorted_vector_test.cpp
sorted_vector_types_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la


foreach_test_SOURCES = ForeachTest.cpp
foreach_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

hash_test_SOURCES = HashTest.cpp
hash_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la


fbstring_test_using_jemalloc_SOURCES = FBStringTest.cpp
fbstring_test_using_jemalloc_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la
TESTS += fbstring_test_using_jemalloc

if HAVE_LINUX
eventfd_test_SOURCES = EventFDTest.cpp
eventfd_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la
TESTS += eventfd_test
endif

thread_cached_int_test_SOURCES = ThreadCachedIntTest.cpp
thread_cached_int_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

thread_local_test_SOURCES = ThreadLocalTest.cpp
thread_local_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

TESTS += thread_cached_int_test thread_local_test

fbvector_test_SOURCES = FBVectorTest.cpp
fbvector_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la
TESTS += fbvector_test

# fails due to cout
dynamic_test_SOURCES = DynamicTest.cpp
dynamic_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la $(top_builddir)/libfolly.la
TESTS += dynamic_test

# fails due to cout
json_test_SOURCES = JsonTest.cpp
json_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la  $(top_builddir)/libfolly.la
TESTS += json_test

benchmark_test_SOURCES = BenchmarkTest.cpp
benchmark_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

# fails due to destructor
scope_guard_test_SOURCES = ScopeGuardTest.cpp
scope_guard_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += scope_guard_test

timeout_queue_test_SOURCES = TimeoutQueueTest.cpp
timeout_queue_test_LDADD = libgtestmain.la $(top_builddir)/libfollytimeout_queue.la

conv_test_SOURCES = ConvTest.cpp
conv_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

range_test_SOURCES = RangeTest.cpp
range_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la

bits_test_SOURCES = BitsTest.cpp
bits_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

bit_iterator_test_SOURCES = BitIteratorTest.cpp
bit_iterator_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la

endian_test_SOURCES = EndianTest.cpp
endian_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += endian_test

rw_spinlock_test_SOURCES = RWSpinLockTest.cpp
rw_spinlock_test_LDADD = libgtestmain.la $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la
TESTS += rw_spinlock_test

synchronized_test_SOURCES = SynchronizedTest.cpp
synchronized_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += synchronized_test

concurrent_skiplist_test_SOURCES = ConcurrentSkipListTest.cpp
concurrent_skiplist_test_LDADD = libgtest.la $(top_builddir)/libfolly.la
TESTS += concurrent_skiplist_test

concurrent_skiplist_benchmark_SOURCES = ConcurrentSkipListBenchmark.cpp
concurrent_skiplist_benchmark_LDADD = $(top_builddir)/libfollybenchmark.la $(top_builddir)/libfolly.la
noinst_PROGRAMS += concurrent_skiplist_benchmark

histogram_test_SOURCES = HistogramTest.cpp
histogram_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += histogram_test

group_varint_test_SOURCES = GroupVarintTest.cpp
group_varint_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += group_varint_test

map_util_test_SOURCES = MapUtilTest.cpp
map_util_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += map_util_test

string_test_SOURCES = StringTest.cpp
string_test_LDADD = libgtest.la $(top_builddir)/libfolly.la $(top_builddir)/libfollybenchmark.la
TESTS += string_test

producer_consumer_queue_test_SOURCES = ProducerConsumerQueueTest.cpp
producer_consumer_queue_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += producer_consumer_queue_test

atomic_hash_array_test_SOURCES = AtomicHashArrayTest.cpp
atomic_hash_array_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += atomic_hash_array_test

atomic_hash_map_test_SOURCES = AtomicHashMapTest.cpp
atomic_hash_map_test_LDADD = libgtest.la $(top_builddir)/libfolly.la $(top_builddir)/libfollybenchmark.la
TESTS += atomic_hash_map_test

format_test_SOURCES = FormatTest.cpp
format_test_LDADD = libgtest.la $(top_builddir)/libfolly.la $(top_builddir)/libfollybenchmark.la
TESTS += format_test

fingerprint_test_SOURCES = FingerprintTest.cpp
fingerprint_test_LDADD = libgtest.la $(top_builddir)/libfolly.la $(top_builddir)/libfollyfingerprint.la $(top_builddir)/libfollybenchmark.la
TESTS += fingerprint_test

portability_test_SOURCES = PortabilityTest.cpp
portability_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += portability_test

cpuid_test_SOURCES = CpuIdTest.cpp
cpuid_test_LDADD = libgtestmain.la $(top_builddir)/libfolly.la
TESTS += cpuid_test

spooky_hash_v1_test_SOURCES = SpookyHashV1Test.cpp
spooky_hash_v1_test_LDADD = $(top_builddir)/libfolly.la $(top_builddir)/libfollybenchmark.la
TESTS += spooky_hash_v1_test

spooky_hash_v2_test_SOURCES = SpookyHashV2Test.cpp
spooky_hash_v2_test_LDADD = $(top_builddir)/libfolly.la $(top_builddir)/libfollybenchmark.la
TESTS += spooky_hash_v2_test

check_PROGRAMS= $(TESTS)
