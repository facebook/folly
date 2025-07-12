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
#include <folly/lang/Align.h>
#include <folly/portability/SysMman.h>

#if FOLLY_HAS_LIBURING

namespace folly {

namespace {

size_t getRefillRingSize(size_t rqEntries) {
  size_t size = rqEntries * sizeof(io_uring_zcrx_rqe);
  // Include space for the header (head/tail/etc.)
  size_t pageSize = sysconf(_SC_PAGESIZE);
  size += pageSize;
  return folly::align_ceil(size, pageSize);
}

constexpr uint64_t kBufferMask = (1ULL << IORING_ZCRX_AREA_SHIFT) - 1;

} // namespace

void IoUringZeroCopyBufferPool::Deleter::operator()(
    IoUringZeroCopyBufferPool* base) {
  if (base) {
    base->destroy();
  }
}

IoUringZeroCopyBufferPool::UniquePtr IoUringZeroCopyBufferPool::create(
    Params params) {
  return IoUringZeroCopyBufferPool::UniquePtr(
      new IoUringZeroCopyBufferPool(params));
}

IoUringZeroCopyBufferPool::IoUringZeroCopyBufferPool(Params params)
    : ring_(params.ring),
      pageSize_(params.pageSize),
      rqEntries_(params.rqEntries),
      bufAreaSize_(params.numPages * params.pageSize),
      buffers_(params.numPages),
      rqRingAreaSize_(getRefillRingSize(params.rqEntries)) {
  for (auto& buf : buffers_) {
    buf.pool = this;
  }
  if (ring_ != nullptr) {
    initialRegister(params.ifindex, params.queueId);
  } else {
    // rqRing_ is set up using information that the kernel fills in via
    // io_uring_register_ifq(). Unit tests do not do this, so fake the rqRing_,
    // specifically ktail and rqes array
    mapMemory();
    rqRing_.khead = nullptr;
    rqRing_.ktail = reinterpret_cast<uint32_t*>(
        static_cast<char*>(rqRingArea_) +
        (rqEntries_ * sizeof(io_uring_zcrx_rqe)));
    rqRing_.rqes = static_cast<io_uring_zcrx_rqe*>(rqRingArea_);
    rqRing_.rq_tail = 0;
    rqRing_.ring_entries = rqEntries_;
  }
}

void IoUringZeroCopyBufferPool::destroy() noexcept {
  std::unique_lock lock{mutex_};
  DCHECK(bufDispensed_ >= rqTail_);
  auto remaining = bufDispensed_ - rqTail_;
  shutdownReferences_ = remaining;
  wantsShutdown_ = true;
  lock.unlock();
  delayedDestroy(remaining);
}

std::unique_ptr<IOBuf> IoUringZeroCopyBufferPool::getIoBuf(
    const io_uring_cqe* cqe, const io_uring_zcrx_cqe* rcqe) noexcept {
  // By the time the pool is being destroyed, IoUringBackend has already drained
  // all requests so there won't be any more calls to getIoBuf().
  DCHECK(!wantsShutdown_);
  auto offset = (rcqe->off & kBufferMask);
  auto length = static_cast<uint32_t>(cqe->res);

  auto freeFn = [](void*, void* userData) {
    auto buffer =
        reinterpret_cast<IoUringZeroCopyBufferPool::Buffer*>(userData);
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
  // This method is only called from an EVB so there is no synchronization.
  bufDispensed_++;
  return ret;
}

void IoUringZeroCopyBufferPool::mapMemory() {
  bufArea_ = ::mmap(
      nullptr,
      bufAreaSize_ + rqRingAreaSize_,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);
  if (bufArea_ == MAP_FAILED) {
    throw std::runtime_error(folly::to<std::string>(
        "IoUringZeroCopyBufferPool failed to mmap size ",
        bufAreaSize_ + rqRingAreaSize_));
  }

  rqRingArea_ = static_cast<char*>(bufArea_) + bufAreaSize_;
}

FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wmissing-designated-field-initializers")

void IoUringZeroCopyBufferPool::initialRegister(
    uint32_t ifindex, uint16_t queueId) {
  mapMemory();

  io_uring_region_desc regionReg = {
      .user_addr = reinterpret_cast<uint64_t>(rqRingArea_),
      .size = rqRingAreaSize_,
      .flags = IORING_MEM_REGION_TYPE_USER,
  };

  io_uring_zcrx_area_reg areaReg = {
      .addr = reinterpret_cast<uint64_t>(bufArea_),
      .len = bufAreaSize_,
      .flags = 0,
  };

  io_uring_zcrx_ifq_reg ifqReg = {
      .if_idx = ifindex,
      .if_rxq = queueId,
      .rq_entries = rqEntries_,
      .area_ptr = reinterpret_cast<uint64_t>(&areaReg),
      .region_ptr = reinterpret_cast<uint64_t>(&regionReg),
  };

  FOLLY_POP_WARNING

  auto ret = io_uring_register_ifq(ring_, &ifqReg);
  if (ret) {
    ::munmap(bufArea_, bufAreaSize_ + rqRingAreaSize_);
    throw std::runtime_error(fmt::format(
        "IoUringZeroCopyBufferPool failed io_uring_register_ifq: {} {}",
        ret,
        ::strerror(ret)));
  }

  rqRing_.khead = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.head));
  rqRing_.ktail = reinterpret_cast<uint32_t*>(
      (static_cast<char*>(rqRingArea_) + ifqReg.offsets.tail));
  rqRing_.rqes = reinterpret_cast<io_uring_zcrx_rqe*>(
      static_cast<char*>(rqRingArea_) + ifqReg.offsets.rqes);
  rqRing_.rq_tail = 0;
  rqRing_.ring_entries = ifqReg.rq_entries;

  rqAreaToken_ = areaReg.rq_area_token;
  rqMask_ = ifqReg.rq_entries - 1;
}

void IoUringZeroCopyBufferPool::returnBuffer(Buffer* buffer) noexcept {
  std::unique_lock lock{mutex_};
  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    auto refs = --shutdownReferences_;
    lock.unlock();
    delayedDestroy(refs);
    return;
  }

  uint32_t myTail = static_cast<uint32_t>(rqTail_++);
  uint32_t nextTail = myTail + 1;

  io_uring_zcrx_rqe* rqe;
  rqe = &rqRing_.rqes[myTail & rqMask_];
  rqe->off = (buffer->off & ~IORING_ZCRX_AREA_MASK) | rqAreaToken_;
  rqe->len = buffer->len;

  // Update the tail and make visible to kernel
  io_uring_smp_store_release(rqRing_.ktail, nextTail);
}

void IoUringZeroCopyBufferPool::delayedDestroy(uint32_t refs) noexcept {
  if (refs == 0) {
    ::munmap(bufArea_, bufAreaSize_ + rqRingAreaSize_);
    delete this;
  }
}

} // namespace folly

#endif
