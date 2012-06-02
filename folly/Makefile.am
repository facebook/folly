SUBDIRS = . test

ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = \
	libfolly.la \
	libfollybenchmark.la \
	libfollytimeout_queue.la

follyincludedir = $(includedir)/folly

nobase_follyinclude_HEADERS = \
	FBVector.h \
	detail/ThreadLocalDetail.h \
	detail/DiscriminatedPtrDetail.h \
	detail/AtomicHashUtils.h \
	detail/BitIteratorDetail.h \
	detail/GroupVarintDetail.h \
	IntrusiveList.h \
	TimeoutQueue.h \
	String.h \
	PackedSyncPtr.h \
	Conv.h \
	ThreadLocal.h \
	ProducerConsumerQueue.h \
	Histogram-inl.h \
	ThreadCachedInt.h \
	ConcurrentSkipList.h \
	json.h \
	folly-config.h \
	FBString.h \
	Unicode.h \
	test/function_benchmark/test_functions.h \
	test/function_benchmark/benchmark_impl.h \
	test/FBStringTestBenchmarks.cpp.h \
	test/SynchronizedTestLib.h \
	test/FBVectorTestBenchmarks.cpp.h \
	test/SynchronizedTestLib-inl.h \
	Synchronized.h \
	Malloc.h \
	dynamic.h \
	AtomicHashArray.h \
	dynamic-inl.h \
	Bits.h \
	sorted_vector_types.h \
	Hash.h \
	DiscriminatedPtr.h \
	ConcurrentSkipList-inl.h \
	Random.h \
	GroupVarint.h \
	Range.h \
	Benchmark.h \
	Likely.h \
	Histogram.h \
	AtomicHashMap.h \
	Portability.h \
	AtomicHashArray-inl.h \
	eventfd.h \
	SmallLocks.h \
	ScopeGuard.h \
	Traits.h \
	RWSpinLock.h \
	small_vector.h \
	Foreach.h \
	AtomicHashMap-inl.h \
	MapUtil.h

FormatTables.cpp: build/generate_format_tables.py
	build/generate_format_tables.py

libfolly_la_SOURCES = \
	Random.cpp \
	Range.cpp \
	Unicode.cpp \
	Conv.cpp \
	Format.cpp \
	FormatTables.cpp \
	String.cpp \
	json.cpp \
	dynamic.cpp \
libfolly_la_LIBADD = $(BOOST_THREAD_LIBS) -lpthread

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = -lboost_regex -lpthread -lrt

libfollytimeout_queue_la_SOURCES = TimeoutQueue.cpp
