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
	AtomicLinkedList.h \
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
	detail/IPAddress.h \
	detail/Malloc.h \
	detail/MemoryIdler.h \
	detail/MPMCPipelineDetail.h \
	detail/SlowFingerprint.h \
	detail/SpinLockImpl.h \
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
	Executor.h \
	EvictingCacheMap.h \
	experimental/AutoTimer.h \
	experimental/Bits.h \
	experimental/EliasFanoCoding.h \
	experimental/EventCount.h \
	experimental/fibers/AddTasks.h \
	experimental/fibers/AddTasks-inl.h \
	experimental/fibers/Baton.h \
	experimental/fibers/Baton-inl.h \
	experimental/fibers/BoostContextCompatibility.h \
	experimental/fibers/EventBaseLoopController.h \
	experimental/fibers/EventBaseLoopController-inl.h \
	experimental/fibers/ExecutionObserver.h \
	experimental/fibers/Fiber.h \
	experimental/fibers/Fiber-inl.h \
	experimental/fibers/FiberIOExecutor.h \
	experimental/fibers/FiberManager.h \
	experimental/fibers/FiberManager-inl.h \
	experimental/fibers/FiberManagerMap.h \
	experimental/fibers/ForEach.h \
	experimental/fibers/ForEach-inl.h \
	experimental/fibers/GenericBaton.h \
	experimental/fibers/GuardPageAllocator.h \
	experimental/fibers/GuardPageAllocator-inl.h \
	experimental/fibers/LoopController.h \
	experimental/fibers/Promise.h \
	experimental/fibers/Promise-inl.h \
	experimental/fibers/SimpleLoopController.h \
	experimental/fibers/TimeoutController.h \
	experimental/fibers/traits.h \
	experimental/fibers/WhenN.h \
	experimental/fibers/WhenN-inl.h \
	experimental/FunctionScheduler.h \
	experimental/io/FsUtil.h \
	experimental/JSONSchema.h \
	experimental/Select64.h \
	experimental/SharedMutex.h \
	experimental/StringKeyedCommon.h \
	experimental/StringKeyedUnorderedMap.h \
	experimental/StringKeyedUnorderedSet.h \
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
	futures/Deprecated.h \
	futures/DrivableExecutor.h \
	futures/Future-pre.h \
	futures/helpers.h \
	futures/Future.h \
	futures/Future-inl.h \
	futures/FutureException.h \
	futures/InlineExecutor.h \
	futures/ManualExecutor.h \
	futures/OpaqueCallbackShunt.h \
	futures/Promise-inl.h \
	futures/Promise.h \
	futures/QueuedImmediateExecutor.h \
	futures/ScheduledExecutor.h \
	futures/Timekeeper.h \
	futures/Try-inl.h \
	futures/Try.h \
	futures/Unit.h \
	futures/detail/Core.h \
	futures/detail/FSM.h \
	futures/detail/ThreadWheelTimekeeper.h \
	futures/detail/Types.h \
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
	io/ShutdownSocketSet.h \
	io/async/AsyncTimeout.h \
	io/async/AsyncTransport.h \
	io/async/AsyncUDPServerSocket.h \
	io/async/AsyncUDPSocket.h \
	io/async/AsyncServerSocket.h \
	io/async/AsyncSignalHandler.h \
	io/async/AsyncSocket.h \
	io/async/AsyncSocketBase.h \
	io/async/AsyncSSLSocket.h \
	io/async/AsyncSocketException.h \
	io/async/DelayedDestruction.h \
	io/async/EventBase.h \
	io/async/EventBaseManager.h \
	io/async/EventFDWrapper.h \
	io/async/EventHandler.h \
	io/async/EventUtil.h \
	io/async/NotificationQueue.h \
	io/async/HHWheelTimer.h \
	io/async/Request.h \
	io/async/SSLContext.h \
	io/async/TimeoutManager.h \
	io/async/test/AsyncSSLSocketTest.h \
	io/async/test/BlockingSocket.h \
	io/async/test/MockAsyncSocket.h \
	io/async/test/MockAsyncServerSocket.h \
	io/async/test/MockAsyncSSLSocket.h \
	io/async/test/MockAsyncTransport.h \
	io/async/test/TimeUtil.h \
	io/async/test/UndelayedDestruction.h \
	io/async/test/Util.h \
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
	Singleton.h \
	Singleton-inl.h \
	SmallLocks.h \
	small_vector.h \
	SocketAddress.h \
	sorted_vector_types.h \
	SpinLock.h \
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
	VersionCheck.h \
	wangle/acceptor/Acceptor.h \
	wangle/acceptor/ConnectionCounter.h \
	wangle/acceptor/ConnectionManager.h \
	wangle/acceptor/DomainNameMisc.h \
	wangle/acceptor/LoadShedConfiguration.h \
	wangle/acceptor/ManagedConnection.h \
	wangle/acceptor/NetworkAddress.h \
	wangle/acceptor/ServerSocketConfig.h \
	wangle/acceptor/SocketOptions.h \
	wangle/acceptor/TransportInfo.h \
	wangle/bootstrap/ServerBootstrap.h \
	wangle/bootstrap/ServerBootstrap-inl.h \
	wangle/bootstrap/ServerSocketFactory.h \
	wangle/bootstrap/ClientBootstrap.h \
	wangle/channel/AsyncSocketHandler.h \
	wangle/channel/EventBaseHandler.h \
	wangle/channel/Handler.h \
	wangle/channel/HandlerContext.h \
	wangle/channel/HandlerContext-inl.h \
	wangle/channel/OutputBufferingHandler.h \
	wangle/channel/Pipeline.h \
	wangle/channel/Pipeline-inl.h \
	wangle/channel/StaticPipeline.h \
	wangle/concurrent/BlockingQueue.h \
	wangle/concurrent/Codel.h \
	wangle/concurrent/CPUThreadPoolExecutor.h \
	wangle/concurrent/FutureExecutor.h \
	wangle/concurrent/IOExecutor.h \
	wangle/concurrent/IOThreadPoolExecutor.h \
	wangle/concurrent/GlobalExecutor.h \
	wangle/concurrent/LifoSemMPMCQueue.h \
	wangle/concurrent/NamedThreadFactory.h \
	wangle/concurrent/ThreadFactory.h \
	wangle/concurrent/ThreadPoolExecutor.h \
	wangle/rx/Observable.h \
	wangle/rx/Observer.h \
	wangle/rx/Subject.h \
	wangle/rx/Subscription.h \
	wangle/rx/types.h \
	wangle/ssl/ClientHelloExtStats.h \
	wangle/ssl/DHParam.h \
	wangle/ssl/PasswordInFile.h \
	wangle/ssl/SSLCacheOptions.h \
	wangle/ssl/SSLCacheProvider.h \
	wangle/ssl/SSLContextConfig.h \
	wangle/ssl/SSLContextManager.h \
	wangle/ssl/SSLSessionCacheManager.h \
	wangle/ssl/SSLStats.h \
	wangle/ssl/SSLUtil.h \
	wangle/ssl/TLSTicketKeyManager.h \
	wangle/ssl/TLSTicketKeySeeds.h

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
	futures/detail/ThreadWheelTimekeeper.cpp \
	futures/Future.cpp \
	futures/InlineExecutor.cpp \
	futures/ManualExecutor.cpp \
	futures/QueuedImmediateExecutor.cpp \
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
	io/ShutdownSocketSet.cpp \
	io/async/AsyncTimeout.cpp \
	io/async/AsyncUDPSocket.cpp \
	io/async/AsyncServerSocket.cpp \
	io/async/AsyncSignalHandler.cpp \
	io/async/AsyncSocket.cpp \
	io/async/AsyncSSLSocket.cpp \
	io/async/EventBase.cpp \
	io/async/EventBaseManager.cpp \
	io/async/EventHandler.cpp \
	io/async/Request.cpp \
	io/async/SSLContext.cpp \
	io/async/HHWheelTimer.cpp \
	io/async/test/TimeUtil.cpp \
	json.cpp \
	detail/MemoryIdler.cpp \
	MacAddress.cpp \
	MemoryMapping.cpp \
	Random.cpp \
	SafeAssert.cpp \
	SocketAddress.cpp \
	Singleton.cpp \
	SpookyHashV1.cpp \
	SpookyHashV2.cpp \
	stats/Instantiations.cpp \
	Subprocess.cpp \
	ThreadCachedArena.cpp \
	TimeoutQueue.cpp \
	Uri.cpp \
	Version.cpp \
	experimental/fibers/Baton.cpp \
	experimental/fibers/Fiber.cpp \
	experimental/fibers/FiberManager.cpp \
	experimental/fibers/FiberManagerMap.cpp \
	experimental/fibers/TimeoutController.cpp \
	experimental/FunctionScheduler.cpp \
	experimental/io/FsUtil.cpp \
	experimental/JSONSchema.cpp \
	experimental/Select64.cpp \
	experimental/SharedMutex.cpp \
	experimental/TestUtil.cpp \
	wangle/acceptor/Acceptor.cpp \
	wangle/acceptor/ConnectionManager.cpp \
	wangle/acceptor/LoadShedConfiguration.cpp \
	wangle/acceptor/ManagedConnection.cpp \
	wangle/acceptor/SocketOptions.cpp \
	wangle/acceptor/TransportInfo.cpp \
	wangle/bootstrap/ServerBootstrap.cpp \
	wangle/concurrent/CPUThreadPoolExecutor.cpp \
	wangle/concurrent/Codel.cpp \
	wangle/concurrent/IOThreadPoolExecutor.cpp \
	wangle/concurrent/GlobalExecutor.cpp \
	wangle/concurrent/ThreadPoolExecutor.cpp \
	wangle/ssl/PasswordInFile.cpp \
	wangle/ssl/SSLContextManager.cpp \
	wangle/ssl/SSLSessionCacheManager.cpp \
	wangle/ssl/SSLUtil.cpp \
	wangle/ssl/TLSTicketKeyManager.cpp

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
libfolly_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION) -lssl

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables
CLEANFILES += FingerprintTables.cpp

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = libfolly.la
libfollybenchmark_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
