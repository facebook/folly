SUBDIRS = . test

ACLOCAL_AMFLAGS = -I m4

CLEANFILES =

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
	AtomicStruct.h \
	Baton.h \
	Benchmark.h \
	Bits.h \
	Chrono.h \
	ConcurrentSkipList.h \
	ConcurrentSkipList-inl.h \
	Conv.h \
	CpuId.h \
	CPortability.h \
	detail/AtomicHashUtils.h \
	detail/BitIteratorDetail.h \
	detail/BitsDetail.h \
	detail/CacheLocality.h \
	detail/DiscriminatedPtrDetail.h \
	detail/FileUtilDetail.h \
	detail/FingerprintPolynomial.h \
	detail/FunctionalExcept.h \
	detail/Futex.h \
	detail/GroupVarintDetail.h \
	detail/Malloc.h \
	detail/MemoryIdler.h \
	detail/MPMCPipelineDetail.h \
	detail/SlowFingerprint.h \
	detail/Stats.h \
	detail/ThreadLocalDetail.h \
	DiscriminatedPtr.h \
	DynamicConverter.h \
	dynamic.h \
	dynamic-inl.h \
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
	IndexedMemPool.h \
	IntrusiveList.h \
	io/Cursor.h \
	io/IOBuf.h \
	io/IOBufQueue.h \
	io/RecordIO.h \
	io/RecordIO-inl.h \
	io/TypedIOBuf.h \
	json.h \
	Lazy.h \
	Likely.h \
	Logging.h \
	Malloc.h \
	MapUtil.h \
	Memory.h \
	MemoryMapping.h \
	MoveWrapper.h \
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
	SpookyHashV1.h \
	SpookyHashV2.h \
	stats/BucketedTimeSeries-defs.h \
	stats/BucketedTimeSeries.h \
	stats/Histogram-defs.h \
	stats/Histogram.h \
	stats/MultiLevelTimeSeries-defs.h \
	stats/MultiLevelTimeSeries.h \
	String.h \
	String-inl.h \
	Subprocess.h \
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
	Unicode.h \
	Uri.h \
	Uri-inl.h \
	Varint.h

FormatTables.cpp: build/generate_format_tables.py
	build/generate_format_tables.py
CLEANFILES += FormatTables.cpp

EscapeTables.cpp: build/generate_escape_tables.py
	build/generate_escape_tables.py
CLEANFILES += EscapeTables.cpp

GroupVarintTables.cpp: build/generate_varint_tables.py
	build/generate_varint_tables.py
CLEANFILES += GroupVarintTables.cpp

libfolly_la_SOURCES = \
	Benchmark.cpp \
	Bits.cpp \
	Conv.cpp \
	detail/CacheLocality.cpp \
	dynamic.cpp \
	EscapeTables.cpp \
	File.cpp \
	FileUtil.cpp \
	Format.cpp \
	FormatTables.cpp \
	GroupVarint.cpp \
	GroupVarintTables.cpp \
	io/IOBuf.cpp \
	io/IOBufQueue.cpp \
	io/RecordIO.cpp \
	json.cpp \
	detail/MemoryIdler.cpp \
	MemoryMapping.cpp \
	Random.cpp \
	Range.cpp \
	SpookyHashV1.cpp \
	SpookyHashV2.cpp \
	stats/Instantiations.cpp \
	String.cpp \
	Subprocess.cpp \
	ThreadCachedArena.cpp \
	TimeoutQueue.cpp \
	Unicode.cpp \
	Uri.cpp

if !HAVE_LINUX
nobase_follyinclude_HEADERS += detail/Clock.h
libfolly_la_SOURCES += detail/Clock.cpp
endif

if !HAVE_WEAK_SYMBOLS
libfolly_la_SOURCES += detail/Malloc.cpp
endif

if !HAVE_BITS_FUNCTEXCEPT
libfolly_la_SOURCES += detail/FunctionalExcept.cpp
endif

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables
CLEANFILES += FingerprintTables.cpp

libfollyfingerprint_la_SOURCES = \
	FingerprintTables.cpp
libfollyfingerprint_la_LIBADD = libfolly.la

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = libfolly.la

libfollytimeout_queue_la_SOURCES = TimeoutQueue.cpp
libfollytimeout_queue_la_LIBADD = libfolly.la

