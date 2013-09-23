SUBDIRS = . test

ACLOCAL_AMFLAGS = -I m4

noinst_PROGRAMS = generate_fingerprint_tables
generate_fingerprint_tables_SOURCES = build/GenerateFingerprintTables.cpp
generate_fingerprint_tables_LDADD = libfolly.la

lib_LTLIBRARIES = \
	libfolly.la \
	libfollybenchmark.la \
	libfollytimeout_queue.la \
	libfollyfingerprint.la

follyincludedir = $(includedir)/folly

nobase_follyinclude_HEADERS = \
	Arena.h \
	Arena-inl.h \
	AtomicHashArray.h \
	AtomicHashArray-inl.h \
	AtomicHashMap.h \
	AtomicHashMap-inl.h \
	Benchmark.h \
	Bits.h \
	ConcurrentSkipList.h \
	ConcurrentSkipList-inl.h \
	Conv.h \
	detail/AtomicHashUtils.h \
	detail/BitIteratorDetail.h \
	detail/DiscriminatedPtrDetail.h \
	detail/FingerprintPolynomial.h \
	detail/GroupVarintDetail.h \
	detail/SlowFingerprint.h \
	detail/ThreadLocalDetail.h \
	DiscriminatedPtr.h \
	dynamic.h \
	dynamic-inl.h \
	eventfd.h \
	FBString.h \
	FBVector.h \
	Fingerprint.h \
	folly-config.h \
	Foreach.h \
	FormatArg.h \
	Format.h \
	Format-inl.h \
	GroupVarint.h \
	Hash.h \
	Histogram.h \
	Histogram-inl.h \
	IntrusiveList.h \
	json.h \
	Likely.h \
	Logging.h \
	Malloc.h \
	MapUtil.h \
	PackedSyncPtr.h \
	Portability.h \
	Preprocessor.h \
	ProducerConsumerQueue.h \
	Random.h \
	Range.h \
	RWSpinLock.h \
	ScopeGuard.h \
	SmallLocks.h \
	small_vector.h \
	sorted_vector_types.h \
	SpookyHashV1.h \
	SpookyHashV2.h \
	StlAllocator.h \
	String.h \
	String-inl.h \
	Synchronized.h \
	test/FBStringTestBenchmarks.cpp.h \
	test/FBVectorTestBenchmarks.cpp.h \
	test/function_benchmark/benchmark_impl.h \
	test/function_benchmark/test_functions.h \
	test/SynchronizedTestLib.h \
	test/SynchronizedTestLib-inl.h \
	ThreadCachedArena.h \
	ThreadCachedInt.h \
	ThreadLocal.h \
	TimeoutQueue.h \
	Traits.h \
	Unicode.h

FormatTables.cpp: build/generate_format_tables.py
	build/generate_format_tables.py

EscapeTables.cpp: build/generate_escape_tables.py
	build/generate_escape_tables.py

GroupVarintTables.cpp: build/generate_varint_tables.py
	build/generate_varint_tables.py

libfolly_la_SOURCES = \
	Random.cpp \
	Range.cpp \
	Unicode.cpp \
	Conv.cpp \
	json.cpp \
	dynamic.cpp \
	GroupVarint.cpp \
	GroupVarintTables.cpp \
	EscapeTables.cpp \
	ThreadCachedArena.cpp \
	Format.cpp \
	FormatTables.cpp \
	String.cpp \
	Bits.cpp \
	SpookyHashV1.cpp \
	SpookyHashV2.cpp

if !HAVE_LINUX
nobase_follyinclude_HEADERS += detail/Clock.h
libfolly_la_SOURCES += detail/Clock.cpp
endif

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables

libfollyfingerprint_la_SOURCES = \
	FingerprintTables.cpp
libfollyfingerprint_la_LIBADD = libfolly.la

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = libfolly.la

libfollytimeout_queue_la_SOURCES = TimeoutQueue.cpp
libfollytimeout_queue_la_LIBADD = libfolly.la

