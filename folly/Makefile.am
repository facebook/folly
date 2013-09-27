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
	ApplyTuple.h \
	Arena.h \
	Arena-inl.h \
	AtomicBitSet.h \
	AtomicHashArray.h \
	AtomicHashArray-inl.h \
	AtomicHashMap.h \
	AtomicHashMap-inl.h \
	Benchmark.h \
	Bits.h \
	Chrono.h \
	ConcurrentSkipList.h \
	ConcurrentSkipList-inl.h \
	Conv.h \
	CpuId.h \
	detail/GroupVarintDetail.h \
	detail/AtomicHashUtils.h \
	detail/FileUtilDetail.h \
	detail/BitIteratorDetail.h \
	detail/Futex.h \
	detail/Stats.h \
	detail/BitsDetail.h \
	detail/MPMCPipelineDetail.h \
	detail/DiscriminatedPtrDetail.h \
	detail/ThreadLocalDetail.h \
	detail/SlowFingerprint.h \
	detail/FingerprintPolynomial.h \
	DiscriminatedPtr.h \
	DynamicConverter.h \
	dynamic.h \
	dynamic-inl.h \
	eventfd.h \
	Exception.h \
	FBString.h \
	FBVector.h \
	File.h \
	FileUtil.h \
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
	io/RecordIO-inl.h \
	io/Cursor.h \
	io/IOBuf.h \
	io/IOBufQueue.h \
	io/TypedIOBuf.h \
	io/Compression.h \
	io/RecordIO.h \
	json.h \
	Lazy.h \
	Likely.h \
	Logging.h \
	Malloc.h \
	MapUtil.h \
	Memory.h \
	MemoryMapping.h \
	MPMCPipeline.h \
	MPMCQueue.h \
	Optional.h \
	PackedSyncPtr.h \
	Padded.h \
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
	SpookyHash.h \
	SpookyHashV1.h \
	SpookyHashV2.h \
	stats/MultiLevelTimeSeries-defs.h \
	stats/BucketedTimeSeries-defs.h \
	stats/MultiLevelTimeSeries.h \
	stats/Histogram-defs.h \
	stats/Histogram.h \
	stats/BucketedTimeSeries.h \
	StlAllocator.h \
	String.h \
	String-inl.h \
	Subprocess.h \
	Synchronized.h \
	test/SynchronizedTestLib.h \
	test/SynchronizedTestLib-inl.h \
	test/stl_tests/OFBVector.h \
	test/DeterministicSchedule.h \
	test/FBStringTestBenchmarks.cpp.h \
	test/FBVectorTestBenchmarks.cpp.h \
	test/function_benchmark/test_functions.h \
	test/function_benchmark/benchmark_impl.h \
	ThreadCachedArena.h \
	ThreadCachedInt.h \
	ThreadLocal.h \
	TimeoutQueue.h \
	Traits.h \
	Unicode.h \
	Uri.h \
	Uri-inl.h \
	Varint.h 

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

libfolly_la_LIBADD = $(BOOST_THREAD_LIB) -lpthread

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables

libfollyfingerprint_la_SOURCES = \
	FingerprintTables.cpp
libfollyfingerprint_la_LIBADD = libfolly.la

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = $(BOOST_REGEX_LIB) -lrt libfolly.la

libfollytimeout_queue_la_SOURCES = TimeoutQueue.cpp
libfollytimeout_queue_la_LIBADD = libfolly.la

