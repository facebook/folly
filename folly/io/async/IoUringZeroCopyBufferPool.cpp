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

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/container/DynamicRingQueue.h>
#include <folly/lang/Align.h>
#include <folly/portability/SysMman.h>
#include <folly/synchronization/DistributedMutex.h>

#if FOLLY_HAS_LIBURING

namespace folly {

class IoUringZeroCopyBufferPoolImpl {
 public:
  friend class IoUringZeroCopyBufferPool;

  struct Buffer {
    uint64_t off;
    uint32_t len;
    IoUringZeroCopyBufferPoolImpl* pool;
  };

  struct SupportedFeatures {
    bool importRing{false};
    bool exportRing{false};
    bool pageSize{false};
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
  size_t getBufferSize() const noexcept { return bufferSize_; }

 private:
  void mapMemory();
  void checkZcRxFeatures();
  void initialRegister(
      const IoUringZeroCopyBufferPool::Params& params, uint32_t pageSize);
  void delayedDestroy(uint32_t refs) noexcept;
  void refillBuffer(Buffer* buffer) noexcept;

  struct io_uring* ring_{nullptr};
  uint32_t bufferSize_{0};
  uint32_t rqEntries_{0};

  void* bufArea_{nullptr};
  size_t bufAreaSize_{0};
  std::vector<Buffer> buffers_;
  void* rqRingArea_{nullptr};
  size_t rqRingAreaSize_{0};
  size_t mmapSize_{0};
  struct io_uring_zcrx_rq rqRing_{};
  uint64_t rqAreaToken_{0};
  uint32_t rqTail_{0};
  unsigned rqMask_{0};
  uint32_t id_{0};
  std::atomic<uint32_t> bufDispensed_{0};

  folly::DistributedMutex mutex_;
  std::atomic<bool> wantsShutdown_{false};
  uint32_t shutdownReferences_{0};
  DynamicRingQueue<Buffer*> pendingBuffers_;
  SupportedFeatures supportedFeatures_{};
};

struct ImplDeleter {
  void operator()(IoUringZeroCopyBufferPoolImpl* impl) const noexcept {
    if (impl) {
      impl->destroy();
    }
  }
};

namespace {
struct RefillRingHeader {
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
constexpr uint32_t kMTU = 4096;

} // namespace

void IoUringZeroCopyBufferPoolImpl::checkZcRxFeatures() {
  struct io_uring_query_zcrx zcrxQuery{};
  struct io_uring_query_hdr hdr{};
  hdr.query_op = IO_URING_QUERY_ZCRX;
  hdr.query_data = reinterpret_cast<__u64>(&zcrxQuery);
  hdr.size = sizeof(zcrxQuery);

  int ret = io_uring_register(
      static_cast<unsigned int>(-1), IORING_REGISTER_QUERY, &hdr, 0);
  if (ret != 0 || hdr.result != 0) {
    return;
  }

  supportedFeatures_.importRing = zcrxQuery.register_flags & ZCRX_REG_IMPORT;
  supportedFeatures_.exportRing = zcrxQuery.nr_ctrl_opcodes > ZCRX_CTRL_EXPORT;
  supportedFeatures_.pageSize = zcrxQuery.features & ZCRX_FEATURE_RX_PAGE_SIZE;
}

IoUringZeroCopyBufferPoolImpl::IoUringZeroCopyBufferPoolImpl(
    IoUringZeroCopyBufferPool::Params params, bool test)
    : ring_(params.ring),
      bufferSize_(params.bufferSizeHint),
      rqEntries_(params.rqEntries),
      buffers_(params.numBuffers),
      rqRingAreaSize_(getRefillRingSize(params.rqEntries)) {
  uint32_t pageSize = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
  if (!bufferSize_) {
    bufferSize_ = pageSize;
  }

  if (bufferSize_ < pageSize || !isPowTwo(bufferSize_)) {
    LOG_FIRST_N(WARNING, 1)
        << "IoUringZeroCopyBufferPool: invalid requested buffer size "
        << bufferSize_ << ", adjusting to " << pageSize;
    bufferSize_ = pageSize;
  }
  checkZcRxFeatures();

  if (bufferSize_ > pageSize && !supportedFeatures_.pageSize) {
    LOG_FIRST_N(WARNING, 1)
        << "IoUringZeroCopyBufferPool: zcrx does not support buffer size, adjusting size to "
        << pageSize;
    bufferSize_ = pageSize;
  }

  bufAreaSize_ = params.numBuffers * bufferSize_;
  mapMemory();

  pendingBuffers_ = DynamicRingQueue<Buffer*>(
      static_cast<uint32_t>(params.numBuffers * (bufferSize_ / kMTU)));

  if (!test) {
    initialRegister(params, pageSize);
  } else {
    // rqRing_ is normally set up using information that the kernel fills in via
    // io_uring_register_ifq(). Unit tests do not do this, so fake it.
    rqRing_.khead = reinterpret_cast<uint32_t*>(
        (static_cast<char*>(rqRingArea_) + offsetof(RefillRingHeader, head)));
    rqRing_.ktail = reinterpret_cast<uint32_t*>(
        (static_cast<char*>(rqRingArea_) + offsetof(RefillRingHeader, tail)));
    rqRing_.rqes = reinterpret_cast<struct io_uring_zcrx_rqe*>(
        static_cast<char*>(rqRingArea_) + sizeof(RefillRingHeader));
    rqRing_.rq_tail = 0;
    rqRing_.ring_entries = rqEntries_;
  }

  // Initialize buffers after registering with zcrx since we can fallback to a
  // lower buffer size.
  DCHECK(!buffers_.empty());
  for (size_t i = 0; i < params.numBuffers; i++) {
    auto& buf = buffers_[i];
    buf.pool = this;
    buf.off = i * bufferSize_;
    buf.len = bufferSize_;
  }
}

IoUringZeroCopyBufferPoolImpl::~IoUringZeroCopyBufferPoolImpl() {
  ::munmap(bufArea_, mmapSize_);
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

  uint32_t i = static_cast<uint32_t>(offset / bufferSize_);
  auto ret = IOBuf::takeOwnership(
      static_cast<char*>(bufArea_) + offset,
      length,
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
  // Align the mmap size to a multiple of 2MB to ensure that we are using
  // the kernel THP mmap path.
  // TODO: Detect smallest huge page size and align to that.
  mmapSize_ = folly::align_ceil(bufAreaSize_ + rqRingAreaSize_, 1 << 21);

  bufArea_ = ::mmap(
      nullptr,
      mmapSize_,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);
  if (bufArea_ == MAP_FAILED) {
    throw std::runtime_error(
        folly::to<std::string>(
            "IoUringZeroCopyBufferPool failed to mmap size ", mmapSize_));
  }

  rqRingArea_ = static_cast<char*>(bufArea_) + bufAreaSize_;

  // Try to promote the buffers to huge pages, if that fails, it isn't
  // fatal, io_uring_register_ifq() will probably fail with a large buffer
  // size but will fallback to the default system page size.
  if (madvise(bufArea_, mmapSize_, MADV_HUGEPAGE) != 0) {
    LOG(WARNING) << "madvise(MADV_HUGEPAGE) failed: " << folly::errnoStr(errno);
  }
}

void IoUringZeroCopyBufferPoolImpl::initialRegister(
    const IoUringZeroCopyBufferPool::Params& params, uint32_t pageSize) {
  struct io_uring_region_desc regionReg{};
  regionReg.user_addr = reinterpret_cast<uint64_t>(rqRingArea_);
  regionReg.size = rqRingAreaSize_;
  regionReg.flags = IORING_MEM_REGION_TYPE_USER;

  struct io_uring_zcrx_area_reg areaReg{};
  areaReg.addr = reinterpret_cast<uint64_t>(bufArea_);
  areaReg.len = bufAreaSize_;

  struct io_uring_zcrx_ifq_reg ifqReg{};
  ifqReg.if_idx = params.ifindex;
  ifqReg.if_rxq = params.queueId;
  ifqReg.rq_entries = rqEntries_;
  ifqReg.area_ptr = reinterpret_cast<uint64_t>(&areaReg);
  ifqReg.region_ptr = reinterpret_cast<uint64_t>(&regionReg);

  if (bufferSize_ > pageSize) {
    ifqReg.rx_buf_len = bufferSize_;
  }

  auto ret = io_uring_register_ifq(ring_, &ifqReg);
  if (bufferSize_ > pageSize && (ret == -EINVAL || ret == -ERANGE)) {
    LOG(WARNING)
        << "IoUringZeroCopyBufferPool failed io_uring_register_ifq with rx_buf_len size "
        << ifqReg.rx_buf_len << ", falling back to default page size";

    ::munmap(bufArea_, mmapSize_);
    bufAreaSize_ = params.numBuffers * pageSize;
    bufferSize_ = pageSize;
    mapMemory();

    ifqReg.rx_buf_len = 0;
    areaReg.addr = reinterpret_cast<uint64_t>(bufArea_);
    areaReg.len = bufAreaSize_;
    regionReg.user_addr = reinterpret_cast<uint64_t>(rqRingArea_);

    ret = io_uring_register_ifq(ring_, &ifqReg);
  }
  if (ret) {
    ::munmap(bufArea_, mmapSize_);
    throw std::runtime_error(
        fmt::format(
            "IoUringZeroCopyBufferPool failed io_uring_register_ifq: {} {}",
            ret,
            folly::errnoStr(-ret)));
  }

  rqRing_.khead = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.head));
  rqRing_.ktail = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.tail));
  rqRing_.rqes = reinterpret_cast<struct io_uring_zcrx_rqe*>(
      static_cast<char*>(rqRingArea_) + ifqReg.offsets.rqes);
  rqRing_.rq_tail = 0;
  rqRing_.ring_entries = ifqReg.rq_entries;

  rqEntries_ = ifqReg.rq_entries;
  rqAreaToken_ = areaReg.rq_area_token;
  rqMask_ = ifqReg.rq_entries - 1;
  id_ = ifqReg.zcrx_id;
}

void IoUringZeroCopyBufferPoolImpl::refillBuffer(Buffer* buffer) noexcept {
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
  uint32_t freeEntries = getRingFreeCount();

  if (freeEntries > 0) {
    refillBuffer(buffer);
    freeEntries--;
  } else {
    pendingBuffers_.push(buffer);
  }

  uint32_t refillCount =
      std::min<uint64_t>(pendingBuffers_.size(), freeEntries);
  for (uint32_t i = 0; i < refillCount; i++) {
    refillBuffer(pendingBuffers_.pop());
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
  if (!handle.impl_->supportedFeatures_.importRing) {
    throw std::runtime_error(
        "IoUringZeroCopyBufferPool import failed: kernel does not support import");
  }

  struct io_uring_zcrx_ifq_reg ifqReg{};
  ifqReg.if_idx = static_cast<uint32_t>(handle.zcrxFd_);
  ifqReg.flags = ZCRX_REG_IMPORT;

  auto ret = io_uring_register_ifq(ring_, &ifqReg);
  if (ret) {
    throw std::runtime_error(
        fmt::format(
            "IoUringZeroCopyBufferPool failed io_uring_register_ifq: {} {}",
            ret,
            folly::errnoStr(-ret)));
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

  if (!impl_->supportedFeatures_.exportRing) {
    throw std::runtime_error(
        "IoUringZeroCopyBufferPool export failed: kernel does not support export");
  }

  struct zcrx_ctrl ctrl{};
  ctrl.zcrx_id = impl_->id_;
  ctrl.op = ZCRX_CTRL_EXPORT;
  auto ret =
      io_uring_register(ring_->ring_fd, IORING_REGISTER_ZCRX_CTRL, &ctrl, 0);
  if (ret < 0) {
    throw std::runtime_error(
        fmt::format(
            "IoUringZeroCopyBufferPool export failed io_uring_register: {} {}",
            ret,
            folly::errnoStr(-ret)));
  }
  auto zcrxFd = static_cast<int>(ctrl.zc_export.zcrx_fd);

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

size_t IoUringZeroCopyBufferPool::getBufferSize() const noexcept {
  return impl_->getBufferSize();
}

} // namespace folly

#endif
