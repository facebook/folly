if FOLLY_TESTMAIN
MAYBE_INIT = init
endif

# Note that the order of SUBDIRS matters.
# Many subdirectories depend on libfollytest from the test directory,
# so it must appear before other directories
SUBDIRS = . test experimental $(MAYBE_INIT) chrono/test io/test stats/test

ACLOCAL_AMFLAGS = -I m4

CLEANFILES =


noinst_PROGRAMS = generate_fingerprint_tables
generate_fingerprint_tables_SOURCES = build/GenerateFingerprintTables.cpp
generate_fingerprint_tables_LDADD = libfollybase.la

noinst_LTLIBRARIES = \
	libfollybasesse42.la \
	libfollybase.la

lib_LTLIBRARIES = \
	libfolly.la \
	libfollybenchmark.la

follyincludedir = $(includedir)/folly

nobase_follyinclude_HEADERS = \
	AtomicBitSet.h \
	AtomicHashArray.h \
	AtomicHashArray-inl.h \
	AtomicHashMap.h \
	AtomicHashMap-inl.h \
	AtomicIntrusiveLinkedList.h \
	AtomicLinkedList.h \
	AtomicUnorderedMap.h \
	Benchmark.h \
	Bits.h \
	CachelinePadded.h \
	Chrono.h \
	chrono/Conv.h \
	ClockGettimeWrappers.h \
	ConcurrentSkipList.h \
	ConcurrentSkipList-inl.h \
	Conv.h \
	CppAttributes.h \
	CpuId.h \
	CPortability.h \
	compression/Compression.h \
	compression/Counters.h \
	compression/Utils.h \
	compression/Zlib.h \
	concurrency/CacheLocality.h \
	concurrency/ConcurrentHashMap.h \
	concurrency/CoreCachedSharedPtr.h \
	concurrency/detail/ConcurrentHashMap-detail.h \
	concurrency/UnboundedQueue.h \
	container/Access.h \
	container/Array.h \
	container/detail/BitIteratorDetail.h \
	container/Iterator.h \
	container/Enumerate.h \
	container/EvictingCacheMap.h \
	container/Foreach.h \
	container/Foreach-inl.h \
	container/SparseByteSet.h \
	ConstexprMath.h \
	detail/AtFork.h \
	detail/AtomicHashUtils.h \
	detail/AtomicUnorderedMapUtils.h \
	detail/DiscriminatedPtrDetail.h \
	detail/FileUtilDetail.h \
	detail/FingerprintPolynomial.h \
	detail/Futex.h \
	detail/GroupVarintDetail.h \
	detail/IPAddress.h \
	detail/IPAddressSource.h \
	detail/MemoryIdler.h \
	detail/MPMCPipelineDetail.h \
	detail/PolyDetail.h \
	detail/RangeCommon.h \
	detail/RangeSse42.h \
	detail/SlowFingerprint.h \
	detail/SocketFastOpen.h \
	detail/StaticSingletonManager.h \
	detail/ThreadLocalDetail.h \
	detail/TypeList.h \
	detail/TurnSequencer.h \
	executors/Async.h \
	executors/CPUThreadPoolExecutor.h \
	executors/Codel.h \
	executors/DrivableExecutor.h \
	executors/FiberIOExecutor.h \
	executors/FutureExecutor.h \
	executors/GlobalExecutor.h \
	executors/GlobalThreadPoolList.h \
	executors/InlineExecutor.h \
	executors/IOExecutor.h \
	executors/IOObjectCache.h \
	executors/IOThreadPoolExecutor.h \
	executors/NotificationQueueExecutor.h \
	executors/ScheduledExecutor.h \
	executors/SerialExecutor.h \
	executors/ThreadPoolExecutor.h \
	executors/ThreadedExecutor.h \
	executors/TimedDrivableExecutor.h \
	executors/task_queue/BlockingQueue.h \
	executors/task_queue/LifoSemMPMCQueue.h \
	executors/task_queue/PriorityLifoSemMPMCQueue.h \
	executors/task_queue/UnboundedBlockingQueue.h \
	executors/thread_factory/NamedThreadFactory.h \
	executors/thread_factory/PriorityThreadFactory.h \
	executors/thread_factory/ThreadFactory.h \
	functional/ApplyTuple.h \
	Demangle.h \
	DiscriminatedPtr.h \
	DynamicConverter.h \
	dynamic.h \
	dynamic-inl.h \
	Exception.h \
	ExceptionString.h \
	ExceptionWrapper.h \
	ExceptionWrapper-inl.h \
	Executor.h \
	Expected.h \
	concurrency/AtomicSharedPtr.h \
	concurrency/detail/AtomicSharedPtr-detail.h \
	experimental/AutoTimer.h \
	experimental/ThreadedRepeatingFunctionRunner.h \
	experimental/Bits.h \
	experimental/BitVectorCoding.h \
	experimental/CodingDetail.h \
	experimental/DynamicParser.h \
	experimental/DynamicParser-inl.h \
	experimental/ExecutionObserver.h \
	experimental/EliasFanoCoding.h \
	experimental/EnvUtil.h \
	experimental/EventCount.h \
	experimental/Instructions.h \
	experimental/bser/Bser.h \
	experimental/exception_tracer/ExceptionAbi.h \
	experimental/exception_tracer/ExceptionCounterLib.h \
	experimental/exception_tracer/ExceptionTracer.h \
	experimental/exception_tracer/ExceptionTracerLib.h \
	experimental/exception_tracer/StackTrace.h \
	experimental/FunctionScheduler.h \
	experimental/FutureDAG.h \
	experimental/hazptr/debug.h \
	experimental/hazptr/hazptr.h \
	experimental/hazptr/hazptr-impl.h \
	experimental/hazptr/memory_resource.h \
	experimental/io/FsUtil.h \
	experimental/JemallocNodumpAllocator.h \
	experimental/JSONSchema.h \
	experimental/LockFreeRingBuffer.h \
	experimental/logging/AsyncFileWriter.h \
	experimental/logging/FileHandlerFactory.h \
	experimental/logging/FileWriterFactory.h \
	experimental/logging/GlogStyleFormatter.h \
	experimental/logging/ImmediateFileWriter.h \
	experimental/logging/Init.h \
	experimental/logging/LogCategory.h \
	experimental/logging/LogCategoryConfig.h \
	experimental/logging/LogConfig.h \
	experimental/logging/LogConfigParser.h \
	experimental/logging/LogFormatter.h \
	experimental/logging/Logger.h \
	experimental/logging/LoggerDB.h \
	experimental/logging/LogHandler.h \
	experimental/logging/LogHandlerFactory.h \
	experimental/logging/LogHandlerConfig.h \
	experimental/logging/LogLevel.h \
	experimental/logging/LogMessage.h \
	experimental/logging/LogName.h \
	experimental/logging/LogStream.h \
	experimental/logging/LogStreamProcessor.h \
	experimental/logging/LogWriter.h \
	experimental/logging/printf.h \
	experimental/logging/RateLimiter.h \
	experimental/logging/StandardLogHandler.h \
	experimental/logging/StandardLogHandlerFactory.h \
	experimental/logging/StreamHandlerFactory.h \
	experimental/logging/xlog.h \
	experimental/NestedCommandLineApp.h \
	experimental/observer/detail/Core.h \
	experimental/observer/detail/GraphCycleDetector.h \
	experimental/observer/detail/ObserverManager.h \
	experimental/observer/detail/Observer-pre.h \
	experimental/observer/Observable.h \
	experimental/observer/Observable-inl.h \
	experimental/observer/Observer.h \
	experimental/observer/Observer-inl.h \
	experimental/observer/SimpleObservable.h \
	experimental/observer/SimpleObservable-inl.h \
	experimental/ProgramOptions.h \
	experimental/ReadMostlySharedPtr.h \
	experimental/Select64.h \
	experimental/StampedPtr.h \
	experimental/StringKeyedCommon.h \
	experimental/StringKeyedMap.h \
	experimental/StringKeyedSet.h \
	experimental/StringKeyedUnorderedMap.h \
	experimental/StringKeyedUnorderedSet.h \
	experimental/TestUtil.h \
	experimental/TLRefCount.h \
	experimental/TupleOps.h \
	FBString.h \
	FBVector.h \
	File.h \
	FileUtil.h \
	Fingerprint.h \
	FixedString.h \
	folly-config.h \
	FormatArg.h \
	FormatTraits.h \
	Format.h \
	Format-inl.h \
	functional/Invoke.h \
	functional/Partial.h \
	futures/Barrier.h \
	futures/Future-pre.h \
	futures/helpers.h \
	futures/Future.h \
	futures/Future-inl.h \
	futures/FutureException.h \
	futures/FutureSplitter.h \
	futures/Promise-inl.h \
	futures/Promise.h \
	futures/SharedPromise.h \
	futures/SharedPromise-inl.h \
	futures/ThreadWheelTimekeeper.h \
	futures/Timekeeper.h \
	futures/detail/Core.h \
	futures/detail/FSM.h \
	futures/detail/Types.h \
	futures/test/TestExecutor.h \
	hash/Checksum.h \
	hash/detail/ChecksumDetail.h \
	hash/Hash.h \
	hash/SpookyHashV1.h \
	hash/SpookyHashV2.h \
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
	Indestructible.h \
	IndexedMemPool.h \
	init/Init.h \
	IntrusiveList.h \
	io/Cursor.h \
	io/Cursor-inl.h \
	io/GlobalShutdownSocketSet.h \
	io/IOBuf.h \
	io/IOBufQueue.h \
	io/RecordIO.h \
	io/RecordIO-inl.h \
	io/TypedIOBuf.h \
	io/ShutdownSocketSet.h \
	io/async/AsyncPipe.h \
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
	io/async/DecoratedAsyncTransportWrapper.h \
	io/async/DelayedDestructionBase.h \
	io/async/DelayedDestruction.h \
	io/async/DestructorCheck.h \
	io/async/EventBase.h \
	io/async/EventBaseLocal.h \
	io/async/EventBaseManager.h \
	io/async/EventBaseThread.h \
	io/async/EventFDWrapper.h \
	io/async/EventHandler.h \
	io/async/EventUtil.h \
	io/async/NotificationQueue.h \
	io/async/HHWheelTimer.h \
	io/async/ssl/OpenSSLUtils.h \
	io/async/ssl/SSLErrors.h \
	io/async/ssl/TLSDefinitions.h \
	io/async/Request.h \
	io/async/SSLContext.h \
	io/async/SSLOptions.h \
	io/async/ScopedEventBaseThread.h \
	io/async/TimeoutManager.h \
	io/async/VirtualEventBase.h \
	io/async/WriteChainAsyncTransportWrapper.h \
	io/async/test/AsyncSSLSocketTest.h \
	io/async/test/AsyncSocketTest2.h \
	io/async/test/BlockingSocket.h \
	io/async/test/MockAsyncSocket.h \
	io/async/test/MockAsyncServerSocket.h \
	io/async/test/MockAsyncSSLSocket.h \
	io/async/test/MockAsyncTransport.h \
	io/async/test/MockAsyncUDPSocket.h \
	io/async/test/MockTimeoutManager.h \
	io/async/test/ScopedBoundPort.h \
	io/async/test/SocketPair.h \
	io/async/test/TestSSLServer.h \
	io/async/test/TimeUtil.h \
	io/async/test/UndelayedDestruction.h \
	io/async/test/Util.h \
	json.h \
	json_pointer.h \
	lang/Align.h \
	lang/Assume.h \
	lang/Bits.h \
	lang/ColdClass.h \
	lang/Launder.h \
	lang/RValueReferenceWrapper.h \
	lang/SafeAssert.h \
	Lazy.h \
	Likely.h \
	LockTraits.h \
	LockTraitsBoost.h \
	Logging.h \
	MacAddress.h \
	MapUtil.h \
	Math.h \
	Memory.h \
	memory/Arena.h \
	memory/Arena-inl.h \
	memory/MallctlHelper.h \
	memory/Malloc.h \
	memory/ThreadCachedArena.h \
	memory/UninitializedMemoryHacks.h \
	memory/detail/MallocImpl.h \
	MicroSpinLock.h \
	MicroLock.h \
	MoveWrapper.h \
	MPMCPipeline.h \
	MPMCQueue.h \
	Optional.h \
	Overload.h \
	PackedSyncPtr.h \
	Padded.h \
	Poly.h \
	Poly-inl.h \
	poly/Nullable.h \
	poly/Regular.h \
	Portability.h \
	portability/Asm.h \
	portability/Atomic.h \
	portability/BitsFunctexcept.h \
	portability/Builtins.h \
	portability/Config.h \
	portability/Constexpr.h \
	portability/Dirent.h \
	portability/Event.h \
	portability/Fcntl.h \
	portability/GFlags.h \
	portability/GMock.h \
	portability/GTest.h \
	portability/IOVec.h \
	portability/Libgen.h \
	portability/Malloc.h \
	portability/Math.h \
	portability/Memory.h \
	portability/OpenSSL.h \
	portability/PThread.h \
	portability/Semaphore.h \
	portability/Sockets.h \
	portability/Stdio.h \
	portability/Stdlib.h \
	portability/String.h \
	portability/Syslog.h \
	portability/SysFile.h \
	portability/SysMembarrier.h \
	portability/SysMman.h \
	portability/SysResource.h \
	portability/SysStat.h \
	portability/SysSyscall.h \
	portability/SysTime.h \
	portability/SysTypes.h \
	portability/SysUio.h \
	portability/Time.h \
	portability/TypeTraits.h \
	portability/Unistd.h \
	portability/Windows.h \
	Preprocessor.h \
	PriorityMPMCQueue.h \
	ProducerConsumerQueue.h \
	Random.h \
	Random-inl.h \
	Range.h \
	Replaceable.h \
	ScopeGuard.h \
	SharedMutex.h \
	Singleton.h \
	Singleton-inl.h \
	SingletonThreadLocal.h \
	small_vector.h \
	SocketAddress.h \
	sorted_vector_types.h \
	SpinLock.h \
	ssl/Init.h \
	ssl/OpenSSLCertUtils.h \
	ssl/OpenSSLHash.h \
	ssl/OpenSSLPtrTypes.h \
	ssl/OpenSSLVersionFinder.h \
	ssl/SSLSession.h \
	ssl/OpenSSLLockTypes.h \
	ssl/detail/OpenSSLThreading.h \
	ssl/detail/SSLSessionImpl.h \
	stats/detail/Bucket.h \
	stats/BucketedTimeSeries-defs.h \
	stats/BucketedTimeSeries.h \
	stats/Histogram-defs.h \
	stats/Histogram.h \
	stats/MultiLevelTimeSeries-defs.h \
	stats/MultiLevelTimeSeries.h \
	stats/TimeseriesHistogram-defs.h \
	stats/TimeseriesHistogram.h \
	synchronization/AsymmetricMemoryBarrier.h \
	synchronization/AtomicStruct.h \
	synchronization/Baton.h \
	synchronization/CallOnce.h \
	synchronization/LifoSem.h \
	synchronization/ParkingLot.h \
	synchronization/PicoSpinLock.h \
	synchronization/RWSpinLock.h \
	synchronization/SaturatingSemaphore.h \
	synchronization/SmallLocks.h \
	synchronization/Tearable.h \
	synchronization/WaitOptions.h \
	synchronization/detail/AtomicUtils.h \
	synchronization/detail/Sleeper.h \
	synchronization/detail/Spin.h \
	system/MemoryMapping.h \
	system/Shell.h \
	system/ThreadId.h \
	system/ThreadName.h \
	system/VersionCheck.h \
	stop_watch.h \
	String.h \
	String-inl.h \
	Subprocess.h \
	Synchronized.h \
	SynchronizedPtr.h \
	test/FBStringTestBenchmarks.cpp.h \
	test/FBVectorTestBenchmarks.cpp.h \
	test/function_benchmark/benchmark_impl.h \
	test/function_benchmark/test_functions.h \
	test/SynchronizedTestLib.h \
	test/SynchronizedTestLib-inl.h \
	test/TestUtils.h \
	ThreadCachedInt.h \
	ThreadLocal.h \
	TimeoutQueue.h \
	TokenBucket.h \
	tracing/StaticTracepoint.h \
	tracing/ScopedTraceSection.h \
	Traits.h \
	Try-inl.h \
	Try.h \
	Unicode.h \
	Function.h \
	UncaughtExceptions.h \
	Unit.h \
	Uri.h \
	Uri-inl.h \
	Utility.h \
	Varint.h

libfollybasesse42_la_SOURCES = \
	detail/RangeSse42.cpp \
	hash/detail/ChecksumDetail.cpp \
	hash/detail/Crc32cDetail.cpp

libfollybase_la_SOURCES = \
	Conv.cpp \
	Demangle.cpp \
	detail/RangeCommon.cpp \
	Format.cpp \
	FormatArg.cpp \
	memory/MallctlHelper.cpp \
	portability/BitsFunctexcept.cpp \
	String.cpp \
	Unicode.cpp

libfolly_la_SOURCES = \
	ClockGettimeWrappers.cpp \
	compression/Compression.cpp \
	compression/Counters.cpp \
	compression/Zlib.cpp \
	concurrency/CacheLocality.cpp \
	detail/AtFork.cpp \
	detail/Futex.cpp \
	detail/IPAddress.cpp \
	detail/StaticSingletonManager.cpp \
	detail/ThreadLocalDetail.cpp \
	dynamic.cpp \
	ExceptionWrapper.cpp \
	Executor.cpp \
	Expected.cpp \
	File.cpp \
	FileUtil.cpp \
	FingerprintTables.cpp \
	futures/Barrier.cpp \
	futures/Future.cpp \
	futures/FutureException.cpp \
	futures/ThreadWheelTimekeeper.cpp \
	futures/test/TestExecutor.cpp \
	executors/CPUThreadPoolExecutor.cpp \
	executors/Codel.cpp \
	executors/GlobalExecutor.cpp \
	executors/GlobalThreadPoolList.cpp \
	executors/IOThreadPoolExecutor.cpp \
	executors/InlineExecutor.cpp \
	executors/ManualExecutor.cpp \
	executors/SerialExecutor.cpp \
	executors/ThreadPoolExecutor.cpp \
	executors/ThreadedExecutor.cpp \
	executors/TimedDrivableExecutor.cpp \
	executors/QueuedImmediateExecutor.cpp \
	experimental/hazptr/hazptr.cpp \
	experimental/hazptr/memory_resource.cpp \
	GroupVarint.cpp \
	hash/Checksum.cpp \
	hash/SpookyHashV1.cpp \
	hash/SpookyHashV2.cpp \
	IPAddress.cpp \
	IPAddressV4.cpp \
	IPAddressV6.cpp \
	init/Init.cpp \
	io/Cursor.cpp \
	io/GlobalShutdownSocketSet.cpp \
	io/IOBuf.cpp \
	io/IOBufQueue.cpp \
	io/RecordIO.cpp \
	io/ShutdownSocketSet.cpp \
	io/async/AsyncPipe.cpp \
	io/async/AsyncTimeout.cpp \
	io/async/AsyncUDPSocket.cpp \
	io/async/AsyncServerSocket.cpp \
	io/async/AsyncSignalHandler.cpp \
	io/async/AsyncSocket.cpp \
	io/async/AsyncSocketException.cpp \
	io/async/AsyncSSLSocket.cpp \
	io/async/EventBase.cpp \
	io/async/EventBaseLocal.cpp \
	io/async/EventBaseManager.cpp \
	io/async/EventBaseThread.cpp \
	io/async/EventHandler.cpp \
	io/async/Request.cpp \
	io/async/SSLContext.cpp \
	io/async/SSLOptions.cpp \
	io/async/ScopedEventBaseThread.cpp \
	io/async/VirtualEventBase.cpp \
	io/async/HHWheelTimer.cpp \
	io/async/TimeoutManager.cpp \
	io/async/test/ScopedBoundPort.cpp \
	io/async/test/SocketPair.cpp \
	io/async/test/TimeUtil.cpp \
	io/async/ssl/OpenSSLUtils.cpp \
	io/async/ssl/SSLErrors.cpp \
	json.cpp \
	json_pointer.cpp \
	lang/Assume.cpp \
	lang/ColdClass.cpp \
	lang/SafeAssert.cpp \
	detail/MemoryIdler.cpp \
	detail/SocketFastOpen.cpp \
	MacAddress.cpp \
	memory/ThreadCachedArena.cpp \
	portability/Dirent.cpp \
	portability/Fcntl.cpp \
	portability/Libgen.cpp \
	portability/Malloc.cpp \
	portability/OpenSSL.cpp \
	portability/PThread.cpp \
	portability/Sockets.cpp \
	portability/Stdio.cpp \
	portability/Stdlib.cpp \
	portability/String.cpp \
	portability/SysFile.cpp \
	portability/SysMembarrier.cpp \
	portability/SysMman.cpp \
	portability/SysResource.cpp \
	portability/SysStat.cpp \
	portability/SysTime.cpp \
	portability/SysUio.cpp \
	portability/Time.cpp \
	portability/Unistd.cpp \
	Random.cpp \
	ScopeGuard.cpp \
	SharedMutex.cpp \
	MicroLock.cpp \
	Optional.cpp \
	Singleton.cpp \
	SocketAddress.cpp \
	ssl/Init.cpp \
	ssl/OpenSSLCertUtils.cpp \
	ssl/OpenSSLHash.cpp \
	ssl/detail/OpenSSLThreading.cpp \
	ssl/detail/SSLSessionImpl.cpp \
	stats/BucketedTimeSeries.cpp \
	stats/Histogram.cpp \
	stats/MultiLevelTimeSeries.cpp \
	stats/TimeseriesHistogram.cpp \
	synchronization/AsymmetricMemoryBarrier.cpp \
	synchronization/LifoSem.cpp \
	synchronization/ParkingLot.cpp \
	synchronization/WaitOptions.cpp \
	system/MemoryMapping.cpp \
	system/Shell.cpp \
	system/ThreadName.cpp \
	system/VersionCheck.cpp \
	Subprocess.cpp \
	TimeoutQueue.cpp \
	Try.cpp \
	Uri.cpp \
	experimental/ThreadedRepeatingFunctionRunner.cpp \
	experimental/bser/Dump.cpp \
	experimental/bser/Load.cpp \
	experimental/DynamicParser.cpp \
	experimental/EnvUtil.cpp \
	experimental/FunctionScheduler.cpp \
	experimental/io/FsUtil.cpp \
	experimental/JemallocNodumpAllocator.cpp \
	experimental/JSONSchema.cpp \
	experimental/NestedCommandLineApp.cpp \
	experimental/observer/detail/Core.cpp \
	experimental/observer/detail/ObserverManager.cpp \
	experimental/ProgramOptions.cpp \
	experimental/Select64.cpp \
	experimental/TestUtil.cpp

if HAVE_LINUX
nobase_follyinclude_HEADERS += \
	experimental/io/HugePages.h
libfolly_la_SOURCES += \
	experimental/io/HugePages.cpp
endif

if !HAVE_WEAK_SYMBOLS
libfollybase_la_SOURCES += memory/detail/MallocImpl.cpp
endif

if HAVE_BOOST_CONTEXT
nobase_follyinclude_HEADERS += \
	fibers/AddTasks.h \
	fibers/AddTasks-inl.h \
	fibers/AtomicBatchDispatcher.h \
	fibers/AtomicBatchDispatcher-inl.h \
	fibers/Baton.h \
	fibers/Baton-inl.h \
	fibers/BatchDispatcher.h \
	fibers/BoostContextCompatibility.h \
	fibers/detail/AtomicBatchDispatcher.h \
	fibers/EventBaseLoopController.h \
	fibers/EventBaseLoopController-inl.h \
	fibers/Fiber.h \
	fibers/Fiber-inl.h \
	fibers/FiberManager.h \
	fibers/FiberManager-inl.h \
	fibers/FiberManagerInternal.h \
	fibers/FiberManagerInternal-inl.h \
	fibers/FiberManagerMap.h \
	fibers/ForEach.h \
	fibers/ForEach-inl.h \
	fibers/GenericBaton.h \
	fibers/GuardPageAllocator.h \
	fibers/LoopController.h \
	fibers/Promise.h \
	fibers/Promise-inl.h \
	fibers/Semaphore.h \
	fibers/SimpleLoopController.h \
	fibers/TimedMutex.h \
	fibers/TimedMutex-inl.h \
	fibers/TimeoutController.h \
	fibers/traits.h \
	fibers/WhenN.h \
	fibers/WhenN-inl.h

libfolly_la_SOURCES += \
	fibers/Baton.cpp \
	fibers/detail/AtomicBatchDispatcher.cpp \
	fibers/Fiber.cpp \
	fibers/FiberManager.cpp \
	fibers/FiberManagerMap.cpp \
	fibers/GuardPageAllocator.cpp \
	fibers/Semaphore.cpp \
	fibers/TimeoutController.cpp
endif

if USE_SYMBOLIZER
nobase_follyinclude_HEADERS += \
	experimental/symbolizer/Elf.h \
	experimental/symbolizer/Elf-inl.h \
	experimental/symbolizer/ElfCache.h \
	experimental/symbolizer/Dwarf.h \
	experimental/symbolizer/LineReader.h \
	experimental/symbolizer/SignalHandler.h \
	experimental/symbolizer/StackTrace.h \
	experimental/symbolizer/Symbolizer.h

libfolly_la_SOURCES += \
	experimental/symbolizer/Elf.cpp \
	experimental/symbolizer/ElfCache.cpp \
	experimental/symbolizer/Dwarf.cpp \
	experimental/symbolizer/LineReader.cpp \
	experimental/symbolizer/SignalHandler.cpp \
	experimental/symbolizer/StackTrace.cpp \
	experimental/symbolizer/Symbolizer.cpp
endif

if HAVE_VARIABLE_TEMPLATES
libfolly_la_SOURCES += \
	Poly.cpp
endif

libfollybasesse42_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)
libfollybasesse42_la_CXXFLAGS = -msse4.2 -mpclmul

libfollybase_la_LIBADD = libfollybasesse42.la
libfollybase_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)

libfolly_la_LIBADD = libfollybase.la
libfolly_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION) -lssl

if USE_SYMBOLIZER
libfolly_la_LDFLAGS += -lunwind
endif

FingerprintTables.cpp: generate_fingerprint_tables
	./generate_fingerprint_tables
CLEANFILES += FingerprintTables.cpp

libfollybenchmark_la_SOURCES = Benchmark.cpp
libfollybenchmark_la_LIBADD = libfolly.la
libfollybenchmark_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LT_VERSION)

# install pkg-config metadata file
pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = libfolly.pc
