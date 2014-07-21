SUBDIRS = . test

ACLOCAL_AMFLAGS = -I m4

CLEANFILES =

noinst_PROGRAMS = generate_fingerprint_tables
generate_fingerprint_tables_SOURCES = build/GenerateFingerprintTables.cpp
generate_fingerprint_tables_LDADD = libfollybase.la

noinst_LTLIBRARIES = \
	libfollybase.la

lib_LTLIBRARIES = \
	libfolly.la \
	libfollybenchmark.la

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
	Checksum.h \
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
	detail/ChecksumDetail.h \
	detail/Clock.h \
	detail/DiscriminatedPtrDetail.h \
	detail/ExceptionWrapper.h \
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
	detail/UncaughtExceptionCounter.h \
	Demangle.h \
	DiscriminatedPtr.h \
	DynamicConverter.h \
	dynamic.h \
	dynamic-inl.h \
	Exception.h \
	ExceptionWrapper.h \
	experimental/Bits.h \
	experimental/EliasFanoCoding.h \
	experimental/EventCount.h \
	experimental/io/FsUtil.h \
	experimental/TestUtil.h \
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
	gen/Base.h \
	gen/Base-inl.h \
	gen/Combine.h \
	gen/Combine-inl.h \
	gen/Core.h \
	gen/Core-inl.h \
	gen/File.h \
	gen/File-inl.h \
	gen/Parallel.h \
	gen/Parallel-inl.h \
	gen/ParallelMap.h \
	gen/ParallelMap-inl.h \
	gen/String.h \
	gen/String-inl.h \
	GroupVarint.h \
	Hash.h \
	IPAddress.h \
	IPAddressV4.h \
	IPAddressV6.h \
	IPAddressException.h \
	IndexedMemPool.h \
	IntrusiveList.h \
	io/Compression.h \
	io/Cursor.h \
	io/IOBuf.h \
	io/IOBufQueue.h \
	io/RecordIO.h \
	io/RecordIO-inl.h \
	io/TypedIOBuf.h \
	io/async/AsyncTimeout.h \
	io/async/EventBase.h \
	io/async/EventFDWrapper.h \
	io/async/EventHandler.h \
	io/async/EventUtil.h \
	io/async/NotificationQueue.h \
	io/async/Request.h \
	io/async/TimeoutManager.h \
	json.h \
	Lazy.h \
	LifoSem.h \
	Likely.h \
	Logging.h \
	MacAddress.h \
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
	Random-inl.h \
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
	ThreadName.h \
	TimeoutQueue.h \
	Traits.h \
	Unicode.h \
	Uri.h \
	Uri-inl.h \
	Varint.h \
	wangle/Executor.h \
	wangle/Future-inl.h \
	wangle/Future.h \
	wangle/GenericThreadGate.h \
	wangle/InlineExecutor.h \
	wangle/Later-inl.h \
	wangle/Later.h \
	wangle/ManualExecutor.h \
	wangle/Promise-inl.h \
	wangle/Promise.h \
	wangle/ThreadGate.h \
	wangle/Try-inl.h \
	wangle/Try.h \
	wangle/WangleException.h \
	wangle/detail/State.h

FormatTables.cpp: build/generate_format_tables.py
	build/generate_format_tables.py
CLEANFILES += FormatTables.cpp

EscapeTables.cpp: build/generate_escape_tables.py
	build/generate_escape_tables.py
CLEANFILES += EscapeTables.cpp

GroupVarintTables.cpp: build/generate_varint_tables.py
	build/generate_varint_tables.py
CLEANFILES += GroupVarintTables.cpp

libfollybase_la_SOURCES = \
	Conv.cpp \
	Demangle.cpp \
	EscapeTables.cpp \
	Format.cpp \
	FormatTables.cpp \
	Malloc.cpp \
	Range.cpp \
	String.cpp \
	Unicode.cpp

libfolly_la_SOURCES = \
	Bits.cpp \
	detail/CacheLocality.cpp \
	dynamic.cpp \
	File.cpp \
	FileUtil.cpp \
	FingerprintTables.cpp \
	detail/Futex.cpp \
	GroupVarint.cpp \
	GroupVarintTables.cpp \
	IPAddress.cpp \
	IPAddressV4.cpp \
	IPAddressV6.cpp \
	LifoSem.cpp \
	io/Compression.cpp \
	io/IOBuf.cpp \
	io/IOBufQueue.cpp \
	io/RecordIO.cpp \
	io/async/AsyncTimeout.cpp \
	io/async/EventBase.cpp \
	io/async/EventHandler.cpp \
	io/async/Request.cpp \
	json.cpp \
	detail/MemoryIdler.cpp \
	MacAddress.cpp \
	MemoryMapping.cpp \
	Random.cpp \
	SafeAssert.cpp \
	SpookyHashV1.cpp \
	SpookyHashV2.cpp \
	stats/Instantiations.cpp \
	Subprocess.cpp \
	ThreadCachedArena.cpp \
	TimeoutQueue.cpp \
	Uri.cpp \
	Version.cpp \
	wangle/InlineExecutor.cpp \
	wangle/ManualExecutor.cpp \
	wangle/ThreadGate.cpp \
	experimental/io/FsUtil.cpp \
	experimental/TestUtil.cpp

if HAVE_LINUX
nobase_follyinclude_HEADERS += \
	experimental/io/HugePages.h
libfolly_la_SOURCES += \
	experimental/io/HugePages.cpp
endif

if !HAVE_LINUX
nobase_follyinclude_HEADERS += detail/Clock.h
libfollybase_la_SOURCES += detail/Clock.cpp
endif

if !HAVE_WEAK_SYMBOLS
libfollybase_la_SOURCES += detail/MallocImpl.cpp
endif

if !HAVE_BITS_FUNCTEXCEPT
libfollybase_la_SOURCES += detail/FunctionalExcept.cpp
endif

libfollybase_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)

libfolly_la_LIBADD = libfollybase.la
libfolly_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables
CLEANFILES += FingerprintTables.cpp

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = libfolly.la
libfollybenchmark_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
