/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>

#if defined(__linux__)
#include <net/if.h>
#endif

#include <folly/Conv.h>
#include <folly/Function.h>
#include <folly/IPAddress.h>
#include <folly/io/async/IoUringZeroCopyBufferPool.h>

namespace folly {
#if FOLLY_HAS_LIBURING

// Callback types for IoUringBackend configuration
using ResolveNapiIdCallback =
    folly::Function<int(int ifindex, uint32_t queueId)>;
using SrcPortForQueueIdCallback = folly::Function<int(
    const folly::IPAddress& destAddr,
    uint16_t destPort,
    int targetQueueId,
    const char* ifname,
    uint16_t startPort,
    uint16_t minPort,
    uint16_t maxPort)>;

struct IoUringOptions {
  enum Flags {
    POLL_SQ = 0x1,
    POLL_CQ = 0x2,
    POLL_SQ_IMMEDIATE_IO = 0x4, // do not enqueue I/O operations
  };

  IoUringOptions() = default;
  IoUringOptions(const IoUringOptions&) = delete;
  IoUringOptions& operator=(const IoUringOptions&) = delete;
  IoUringOptions(IoUringOptions&&) = default;
  IoUringOptions& operator=(IoUringOptions&&) = default;
  ~IoUringOptions() = default;

  IoUringOptions& setCapacity(size_t v) {
    capacity = v;
    return *this;
  }

  IoUringOptions& setMinCapacity(size_t v) {
    minCapacity = v;

    return *this;
  }

  IoUringOptions& setMaxSubmit(size_t v) {
    maxSubmit = v;

    return *this;
  }

  IoUringOptions& setSqeSize(size_t v) {
    sqeSize = v;

    return *this;
  }

  IoUringOptions& setMaxGet(size_t v) {
    maxGet = v;

    return *this;
  }

  IoUringOptions& setUseRegisteredFds(size_t v) {
    registeredFds = v;
    return *this;
  }

  IoUringOptions& setFlags(uint32_t v) {
    flags = v;

    return *this;
  }

  IoUringOptions& setSQIdle(std::chrono::milliseconds v) {
    sqIdle = v;

    return *this;
  }

  IoUringOptions& setCQIdle(std::chrono::milliseconds v) {
    cqIdle = v;

    return *this;
  }

  // Set the CPU as preferred for submission queue poll thread.
  //
  // This only has effect if POLL_SQ flag is specified.
  //
  // Can call multiple times to specify multiple CPUs.
  IoUringOptions& setSQCpu(uint32_t v) {
    sqCpus.insert(v);

    return *this;
  }

  // Set the preferred CPUs for submission queue poll thread(s).
  //
  // This only has effect if POLL_SQ flag is specified.
  IoUringOptions& setSQCpus(std::set<uint32_t> const& cpus) {
    sqCpus.insert(cpus.begin(), cpus.end());

    return *this;
  }

  IoUringOptions& setSQGroupName(const std::string& v) {
    sqGroupName = v;

    return *this;
  }

  IoUringOptions& setSQGroupNumThreads(size_t v) {
    sqGroupNumThreads = v;

    return *this;
  }

  IoUringOptions& setInitialProvidedBuffers(size_t eachSize, size_t count) {
    initialProvidedBuffersCount = count;
    initialProvidedBuffersEachSize = eachSize;
    return *this;
  }

  constexpr bool isPow2(uint64_t n) noexcept { return n > 0 && !((n - 1) & n); }

  IoUringOptions& setProvidedBufRings(size_t v) {
    if (!isPow2(v)) {
      throw std::runtime_error(
          folly::to<std::string>(
              "number of provided buffer rings must be a power of 2"));
    }
    providedBufRings = v;
    return *this;
  }

  IoUringOptions& setRegisterRingFd(bool v) {
    registerRingFd = v;

    return *this;
  }

  IoUringOptions& setTaskRunCoop(bool v) {
    taskRunCoop = v;

    return *this;
  }

  IoUringOptions& setDeferTaskRun(bool v) {
    deferTaskRun = v;

    return *this;
  }

  IoUringOptions& setTimeout(std::chrono::microseconds v) {
    timeout = v;

    return *this;
  }

  IoUringOptions& setBatchSize(int v) {
    batchSize = v;

    return *this;
  }

  IoUringOptions& setZeroCopyRx(bool v) {
    zeroCopyRx = v;

    return *this;
  }

  IoUringOptions& setZeroCopyRxInterface(std::string v) {
    zcRxIfname = std::move(v);
    zcRxIfindex = ::if_nametoindex(zcRxIfname.c_str());
    if (zcRxIfindex == 0) {
      throw std::runtime_error(
          folly::to<std::string>(
              "invalid network interface name: ",
              zcRxIfname,
              ", errno: ",
              errno));
    }

    return *this;
  }

  IoUringOptions& setZeroCopyRxQueue(int queueId) {
    zcRxQueueId = queueId;

    return *this;
  }

  IoUringOptions& setResolveNapiCallback(ResolveNapiIdCallback&& v) {
    resolveNapiId = std::move(v);

    return *this;
  }

  IoUringOptions& setZcrxSrcPortCallback(SrcPortForQueueIdCallback&& v) {
    srcPortQueueId = std::move(v);

    return *this;
  }

  IoUringOptions& setZeroCopyRxNumPages(int v) {
    zcRxNumPages = v;

    return *this;
  }

  IoUringOptions& setZeroCopyRxRefillEntries(int v) {
    zcRxRefillEntries = v;

    return *this;
  }

  IoUringOptions& setEnableIncrementalBuffers(bool v) {
    enableIncrementalBuffers = v;

    return *this;
  }

  IoUringOptions& setUseHugePages(bool v) {
    useHugePages = v;

    return *this;
  }

  IoUringOptions& setNativeAsyncSocketSupport(bool v) {
    nativeAsyncSocketSupport = v;

    return *this;
  }

  IoUringOptions& setBufferPoolHandle(
      IoUringZeroCopyBufferPool::ExportHandle&& v) {
    bufferPoolHandle = std::move(v);
    return *this;
  }

  IoUringOptions& setArenaRegion(void* base, size_t size, uint32_t index) {
    arenaRegion.iov_base = base;
    arenaRegion.iov_len = size;
    arenaIndex = index;
    return *this;
  }

  ssize_t sqeSize{-1};

  size_t capacity{256};
  size_t minCapacity{0};
  size_t maxSubmit{128};
  size_t maxGet{256};
  size_t registeredFds{0};
  size_t sqGroupNumThreads{1};
  size_t initialProvidedBuffersCount{0};
  size_t initialProvidedBuffersEachSize{0};
  size_t providedBufRings{1};

  uint32_t flags{0};

  // Minimum number of requests (defined as sockets with data to read) to wait
  // for per io_uring_enter
  int batchSize{0};

  bool registerRingFd{false};
  bool taskRunCoop{false};
  bool deferTaskRun{false};

  // Maximum amount of time to wait (in microseconds) per io_uring_enter
  // Both timeout _and_ batchSize must be set for io_uring_enter wait_nr to be
  // set!
  std::chrono::microseconds timeout{0};
  std::chrono::milliseconds sqIdle{0};
  std::chrono::milliseconds cqIdle{0};

  std::set<uint32_t> sqCpus;

  std::string sqGroupName;

  // Zero copy receive
  bool zeroCopyRx{false};
  std::string zcRxIfname;
  int zcRxQueueId{-1};
  int zcRxIfindex{-1};
  ResolveNapiIdCallback resolveNapiId;
  SrcPortForQueueIdCallback srcPortQueueId;
  int zcRxNumPages{-1};
  int zcRxRefillEntries{-1};

  // Incremental Buffers
  bool enableIncrementalBuffers{false};
  bool useHugePages{false};

  bool nativeAsyncSocketSupport{false};

  std::optional<IoUringZeroCopyBufferPool::ExportHandle> bufferPoolHandle =
      std::nullopt;

  struct iovec arenaRegion{};
  uint32_t arenaIndex{0};
};
#endif

} // namespace folly
