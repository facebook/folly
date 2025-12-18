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

#include <folly/io/async/IoUringZeroCopyBufferPool.h>

#include <mutex>
#include <queue>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/lang/Align.h>
#include <folly/portability/SysMman.h>
#include <folly/synchronization/DistributedMutex.h>

#if FOLLY_HAS_LIBURING

// TODO(davidhwei): Remove once liburing catches up and gets synced with
// fbsource.
#define IORING_REGISTER_ZCRX_CTRL 36

enum zcrx_reg_flags {
  ZCRX_REG_IMPORT = 1,
};

enum zcrx_ctrl_op {
  ZCRX_CTRL_FLUSH_RQ,
  ZCRX_CTRL_EXPORT,

  __ZCRX_CTRL_LAST,
};

struct zcrx_ctrl_flush_rq {
  __u64 __resv[6];
};

struct zcrx_ctrl_export {
  __u32 zcrx_fd;
  __u32 __resv1[11];
};

struct zcrx_ctrl {
  __u32 zcrx_id;
  __u32 op; /* see enum zcrx_ctrl_op */
  __u64 __resv[2];

  union {
    struct zcrx_ctrl_export zc_export;
    struct zcrx_ctrl_flush_rq zc_flush;
  };
};

namespace folly {

class IoUringZeroCopyBufferPoolImpl {
 public:
  friend class IoUringZeroCopyBufferPool;

  struct Buffer {
    uint64_t off;
    uint32_t len;
    IoUringZeroCopyBufferPoolImpl* pool;
  };

  explicit IoUringZeroCopyBufferPoolImpl(
      IoUringZeroCopyBufferPool::Params params, bool test);

  ~IoUringZeroCopyBufferPoolImpl();

  IoUringZeroCopyBufferPoolImpl(IoUringZeroCopyBufferPoolImpl&&) = delete;
  IoUringZeroCopyBufferPoolImpl(IoUringZeroCopyBufferPoolImpl const&) = delete;
  IoUringZeroCopyBufferPoolImpl& operator=(IoUringZeroCopyBufferPoolImpl&&) =
      delete;
  IoUringZeroCopyBufferPoolImpl& operator=(
      IoUringZeroCopyBufferPoolImpl const&) = delete;

  void destroy() noexcept;

  std::unique_ptr<IOBuf> getIoBuf(
      const struct io_uring_cqe* cqe,
      const struct io_uring_zcrx_cqe* rcqe) noexcept;
  void returnBuffer(Buffer* buf) noexcept;

  // For testing
  uint32_t* getHead() const noexcept { return rqRing_.khead; }
  uint32_t getRingUsedCount() const noexcept {
    return rqTail_ - io_uring_smp_load_acquire(rqRing_.khead);
  }
  uint32_t getRingFreeCount() const noexcept {
    return rqEntries_ - getRingUsedCount();
  }
  size_t getPendingBuffersSize() const noexcept {
    return pendingBuffers_.size();
  }
  uint32_t getFlushThreshold() const noexcept { return flushThreshold_; }
  uint16_t getAndResetFlushFailures() noexcept {
    return flushFailures_.exchange(0, std::memory_order_relaxed);
  }
  uint16_t getAndResetFlushCount() noexcept {
    return flushCount_.exchange(0, std::memory_order_relaxed);
  }

 private:
  void mapMemory();
  void initialRegister(uint32_t ifindex, uint16_t queueId);
  void delayedDestroy(uint32_t refs) noexcept;
  uint32_t getRingQueuedCount() const noexcept;
  void writeBufferToRing(Buffer* buffer) noexcept;
  void flushRefillQueue() noexcept;

  struct io_uring* ring_{nullptr};
  size_t pageSize_{0};
  uint32_t rqEntries_{0};

  void* bufArea_{nullptr};
  size_t bufAreaSize_{0};
  std::vector<Buffer> buffers_;
  void* rqRingArea_{nullptr};
  size_t rqRingAreaSize_{0};
  struct io_uring_zcrx_rq rqRing_{};
  uint64_t rqAreaToken_{0};
  uint32_t rqTail_{0};
  unsigned rqMask_{0};
  uint32_t id_{0};
  std::atomic<uint32_t> bufDispensed_{0};

  folly::DistributedMutex mutex_;
  std::atomic<bool> wantsShutdown_{false};
  uint32_t shutdownReferences_{0};
  std::queue<Buffer*> pendingBuffers_;
  uint32_t flushThreshold_{128};
  std::atomic<uint16_t> flushFailures_{0};
  std::atomic<uint16_t> flushCount_{0};
};

struct ImplDeleter {
  void operator()(IoUringZeroCopyBufferPoolImpl* impl) const noexcept {
    if (impl) {
      impl->destroy();
    }
  }
};

namespace {
struct RingQueue {
  uint32_t head;
  uint32_t tail;
};

size_t getRefillRingSize(size_t rqEntries) {
  size_t size = rqEntries * sizeof(struct io_uring_zcrx_rqe);
  // Include space for the header (head/tail/etc.)
  size_t pageSize = sysconf(_SC_PAGESIZE);
  size += pageSize;
  return folly::align_ceil(size, pageSize);
}

constexpr uint64_t kBufferMask = (1ULL << IORING_ZCRX_AREA_SHIFT) - 1;

} // namespace

IoUringZeroCopyBufferPoolImpl::IoUringZeroCopyBufferPoolImpl(
    IoUringZeroCopyBufferPool::Params params, bool test)
    : ring_(params.ring),
      pageSize_(params.pageSize),
      rqEntries_(params.rqEntries),
      bufAreaSize_(params.numPages * params.pageSize),
      buffers_(params.numPages),
      rqRingAreaSize_(getRefillRingSize(params.rqEntries)) {
  for (auto& buf : buffers_) {
    buf.pool = this;
  }
  mapMemory();

  // Calculate flush threshold to bound pending queue growth.
  //
  // When pending buffers reach this threshold, we flush to the kernel which
  // drains entries from the refill ring, freeing space for pending buffers.
  //
  // Threshold = max(128, min(rqEntries/4, numPages/20)):
  // - rqEntries/4 (25% of ring): scales with kernel's drain capacity per flush
  // - numPages/20 (5% of pool): caps memory sitting idle in pending queue
  // - min(): use the smaller limit to respect both constraints
  // - max(128): floor of 4 × ZCRX_FLUSH_BATCH to ensure batching efficiency
  constexpr uint32_t kKernelFlushBatch = 32;
  constexpr uint32_t kMinFlushThreshold = 4 * kKernelFlushBatch;
  flushThreshold_ = std::max(
      kMinFlushThreshold,
      std::min(
          static_cast<uint32_t>(rqEntries_ / 4), // 25% of ring
          static_cast<uint32_t>(params.numPages / 20))); // 5% of pool

  if (!test) {
    initialRegister(params.ifindex, params.queueId);
  } else {
    // rqRing_ is normally set up using information that the kernel fills in via
    // io_uring_register_ifq(). Unit tests do not do this, so fake it.
    rqRing_.khead = reinterpret_cast<uint32_t*>(
        (static_cast<char*>(rqRingArea_) + offsetof(RingQueue, head)));
    rqRing_.ktail = reinterpret_cast<uint32_t*>(
        (static_cast<char*>(rqRingArea_) + offsetof(RingQueue, tail)));
    rqRing_.rqes = reinterpret_cast<struct io_uring_zcrx_rqe*>(
        static_cast<char*>(rqRingArea_) + sizeof(RingQueue));
    rqRing_.rq_tail = 0;
    rqRing_.ring_entries = rqEntries_;
  }
}

IoUringZeroCopyBufferPoolImpl::~IoUringZeroCopyBufferPoolImpl() {
  ::munmap(bufArea_, bufAreaSize_ + rqRingAreaSize_);
}

void IoUringZeroCopyBufferPoolImpl::destroy() noexcept {
  std::unique_lock lock{mutex_};
  DCHECK(bufDispensed_ >= rqTail_);
  auto remaining = bufDispensed_.load(std::memory_order_relaxed) - rqTail_;
  // Drain refs in overflow queue
  remaining -= pendingBuffers_.size();
  shutdownReferences_ = remaining;
  wantsShutdown_ = true;
  lock.unlock();
  delayedDestroy(remaining);
}

std::unique_ptr<IOBuf> IoUringZeroCopyBufferPoolImpl::getIoBuf(
    const struct io_uring_cqe* cqe,
    const struct io_uring_zcrx_cqe* rcqe) noexcept {
  // By the time the pool is being destroyed, IoUringBackend has already drained
  // all requests so there won't be any more calls to getIoBuf().
  DCHECK(!wantsShutdown_);
  auto offset = (rcqe->off & kBufferMask);
  auto length = static_cast<uint32_t>(cqe->res);

  auto freeFn = [](void*, void* userData) {
    auto buffer =
        reinterpret_cast<IoUringZeroCopyBufferPoolImpl::Buffer*>(userData);
    DCHECK(buffer->pool);
    buffer->pool->returnBuffer(buffer);
  };

  int i = offset / pageSize_;
  auto& buf = buffers_[i];
  buf.off = rcqe->off;
  buf.len = cqe->res;

  auto ret = IOBuf::takeOwnership(
      static_cast<char*>(bufArea_) + offset,
      pageSize_,
      length,
      freeFn,
      &buffers_[i]);
  // The underlying buffers are shared between userspace and kernel. The IOBufs
  // only 'wrap' the data and is read-only. Mark as shared such that downstream
  // users of this IOBuf do not try to destructively modify the data.
  ret->markExternallySharedOne();
  bufDispensed_.fetch_add(1, std::memory_order_relaxed);
  return ret;
}

void IoUringZeroCopyBufferPoolImpl::mapMemory() {
  bufArea_ = ::mmap(
      nullptr,
      bufAreaSize_ + rqRingAreaSize_,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);
  if (bufArea_ == MAP_FAILED) {
    throw std::runtime_error(
        folly::to<std::string>(
            "IoUringZeroCopyBufferPool failed to mmap size ",
            bufAreaSize_ + rqRingAreaSize_));
  }

  rqRingArea_ = static_cast<char*>(bufArea_) + bufAreaSize_;
}

void IoUringZeroCopyBufferPoolImpl::initialRegister(
    uint32_t ifindex, uint16_t queueId) {
  struct io_uring_region_desc regionReg{};
  regionReg.user_addr = reinterpret_cast<uint64_t>(rqRingArea_);
  regionReg.size = rqRingAreaSize_;
  regionReg.flags = IORING_MEM_REGION_TYPE_USER;

  struct io_uring_zcrx_area_reg areaReg{};
  areaReg.addr = reinterpret_cast<uint64_t>(bufArea_);
  areaReg.len = bufAreaSize_;

  struct io_uring_zcrx_ifq_reg ifqReg{};
  ifqReg.if_idx = ifindex;
  ifqReg.if_rxq = queueId;
  ifqReg.rq_entries = rqEntries_;
  ifqReg.area_ptr = reinterpret_cast<uint64_t>(&areaReg);
  ifqReg.region_ptr = reinterpret_cast<uint64_t>(&regionReg);

  auto ret = io_uring_register_ifq(ring_, &ifqReg);
  if (ret) {
    ::munmap(bufArea_, bufAreaSize_ + rqRingAreaSize_);
    throw std::runtime_error(
        fmt::format(
            "IoUringZeroCopyBufferPool failed io_uring_register_ifq: {} {}",
            ret,
            folly::errnoStr(ret)));
  }

  rqRing_.khead = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.head));
  rqRing_.ktail = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.tail));
  rqRing_.rqes = reinterpret_cast<struct io_uring_zcrx_rqe*>(
      static_cast<char*>(rqRingArea_) + ifqReg.offsets.rqes);
  rqRing_.rq_tail = 0;
  rqRing_.ring_entries = ifqReg.rq_entries;

  rqAreaToken_ = areaReg.rq_area_token;
  rqMask_ = ifqReg.rq_entries - 1;
  id_ = ifqReg.zcrx_id;
}

uint32_t IoUringZeroCopyBufferPoolImpl::getRingQueuedCount() const noexcept {
  return rqTail_ - io_uring_smp_load_acquire(rqRing_.khead);
}

void IoUringZeroCopyBufferPoolImpl::writeBufferToRing(Buffer* buffer) noexcept {
  uint32_t myTail = rqTail_++;

  struct io_uring_zcrx_rqe* rqe = &rqRing_.rqes[myTail & rqMask_];
  rqe->off = (buffer->off & ~IORING_ZCRX_AREA_MASK) | rqAreaToken_;
  rqe->len = buffer->len;
}

void IoUringZeroCopyBufferPoolImpl::returnBuffer(Buffer* buffer) noexcept {
  std::unique_lock lock{mutex_};
  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    auto refs = --shutdownReferences_;
    lock.unlock();
    delayedDestroy(refs);
    return;
  }

  uint32_t startTail = rqTail_;

  // Trigger flush when pending buffers reach threshold computed from:
  // max(128, min(25% of ring, 5% of pool))
  // This bounds pending queue growth per Little's Law: L = λW
  if (pendingBuffers_.size() >= flushThreshold_) {
    flushRefillQueue();
  }

  uint32_t queueLength = getRingQueuedCount();
  uint32_t slots = rqRing_.ring_entries - queueLength;
  auto numToProcess =
      std::min(pendingBuffers_.size(), static_cast<size_t>(slots));
  for (size_t i = 0; i < numToProcess; i++) {
    writeBufferToRing(pendingBuffers_.front());
    pendingBuffers_.pop();
  }

  if (numToProcess < slots) {
    writeBufferToRing(buffer);
  } else {
    pendingBuffers_.push(buffer);
  }

  if (rqTail_ != startTail) {
    io_uring_smp_store_release(rqRing_.ktail, rqTail_);
  }
}

void IoUringZeroCopyBufferPoolImpl::delayedDestroy(uint32_t refs) noexcept {
  if (refs == 0) {
    delete this;
  }
}

void IoUringZeroCopyBufferPoolImpl::flushRefillQueue() noexcept {
  // ring_ is nullptr in unit tests which don't have a real io_uring.
  if (ring_ == nullptr) {
    return;
  }
  struct zcrx_ctrl ctrl{};
  ctrl.zcrx_id = id_;
  ctrl.op = ZCRX_CTRL_FLUSH_RQ;

  // Best-effort flush: on failure (e.g., -EOPNOTSUPP on older kernels),
  // buffers remain in pendingBuffers_ and are processed when ring space
  // becomes available naturally.
  int ret =
      io_uring_register(ring_->ring_fd, IORING_REGISTER_ZCRX_CTRL, &ctrl, 0);
  flushCount_.fetch_add(1, std::memory_order_relaxed);
  if (ret < 0) {
    flushFailures_.fetch_add(1, std::memory_order_relaxed);
    LOG_EVERY_N(WARNING, 100)
        << "Zero copy receive refill queue flush failed: "
        << folly::errnoStr(-ret);
  }
}

/*static*/ IoUringZeroCopyBufferPool::UniquePtr
IoUringZeroCopyBufferPool::create(Params params) {
  return IoUringZeroCopyBufferPool::UniquePtr(
      new IoUringZeroCopyBufferPool(params));
}

/*static*/ IoUringZeroCopyBufferPool::UniquePtr
IoUringZeroCopyBufferPool::importHandle(
    ExportHandle handle, struct io_uring* ring) {
  return IoUringZeroCopyBufferPool::UniquePtr(
      new IoUringZeroCopyBufferPool(std::move(handle), ring));
}

IoUringZeroCopyBufferPool::IoUringZeroCopyBufferPool(Params params)
    : ring_(params.ring),
      impl_(new IoUringZeroCopyBufferPoolImpl(params, false), ImplDeleter{}) {
  zcrxId_ = impl_->id_;
}

IoUringZeroCopyBufferPool::IoUringZeroCopyBufferPool(Params params, TestTag)
    : ring_(params.ring),
      impl_(new IoUringZeroCopyBufferPoolImpl(params, true), ImplDeleter{}) {}

IoUringZeroCopyBufferPool::IoUringZeroCopyBufferPool(
    ExportHandle handle, struct io_uring* ring)
    : ring_(ring) {
  struct io_uring_zcrx_ifq_reg ifqReg{};
  ifqReg.if_idx = static_cast<uint32_t>(handle.zcrxFd_);
  ifqReg.flags = ZCRX_REG_IMPORT;

  auto ret = io_uring_register_ifq(ring_, &ifqReg);
  if (ret) {
    throw std::runtime_error(
        fmt::format(
            "IoUringZeroCopyBufferPool failed io_uring_register_ifq: {} {}",
            ret,
            folly::errnoStr(ret)));
  }

  zcrxId_ = ifqReg.zcrx_id;
  zcrxFd_ = handle.zcrxFd_;
  impl_ = std::move(handle.impl_);
}

IoUringZeroCopyBufferPool::~IoUringZeroCopyBufferPool() {
  if (zcrxFd_ >= 0) {
    close(zcrxFd_);
  }
}

IoUringZeroCopyBufferPool::ExportHandle
IoUringZeroCopyBufferPool::exportHandle() const {
  if (impl_->ring_ != ring_) {
    throw std::runtime_error(
        "Cannot export a handle from a non-owning IoUringZeroCopyBufferPool");
  }

  struct zcrx_ctrl ctrl{};
  ctrl.zcrx_id = impl_->id_;
  ctrl.op = ZCRX_CTRL_EXPORT;
  auto zcrxFd =
      io_uring_register(ring_->ring_fd, IORING_REGISTER_ZCRX_CTRL, &ctrl, 0);

  return ExportHandle(zcrxFd, impl_);
}

std::unique_ptr<IOBuf> IoUringZeroCopyBufferPool::getIoBuf(
    const struct io_uring_cqe* cqe,
    const struct io_uring_zcrx_cqe* rcqe) noexcept {
  return impl_->getIoBuf(cqe, rcqe);
}

uint32_t* IoUringZeroCopyBufferPool::getHead() const noexcept {
  return impl_->getHead();
}

uint32_t IoUringZeroCopyBufferPool::getRingUsedCount() const noexcept {
  return impl_->getRingUsedCount();
}

uint32_t IoUringZeroCopyBufferPool::getRingFreeCount() const noexcept {
  return impl_->getRingFreeCount();
}

size_t IoUringZeroCopyBufferPool::getPendingBuffersSize() const noexcept {
  return impl_->getPendingBuffersSize();
}

uint32_t IoUringZeroCopyBufferPool::getFlushThreshold() const noexcept {
  return impl_->getFlushThreshold();
}

uint16_t IoUringZeroCopyBufferPool::getAndResetFlushFailures() noexcept {
  return impl_->getAndResetFlushFailures();
}

uint16_t IoUringZeroCopyBufferPool::getAndResetFlushCount() noexcept {
  return impl_->getAndResetFlushCount();
}

} // namespace folly

#endif
