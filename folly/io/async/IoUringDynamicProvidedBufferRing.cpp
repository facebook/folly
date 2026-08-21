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

#include <folly/io/async/IoUringDynamicProvidedBufferRing.h>

#include <algorithm>
#include <new>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/lang/Align.h>
#include <folly/portability/SysMman.h>

#if FOLLY_HAS_LIBURING

namespace {
constexpr uint32_t kMinBufferSize = 32;
constexpr uint32_t kHugePageSizeBytes = 1024 * 1024 * 2;
constexpr uint32_t kBufferAlignBytes = 32;
constexpr uint32_t kMaxRingRefillEntries = 32768;
constexpr uint32_t kInitialAreaCount = 2;
constexpr uint32_t kMaxAreaCount = 64;
} // namespace

namespace folly {

void IoUringDynamicProvidedBufferRing::checkInvariants() {
  // This object is carefully packed into two 64 byte cache lines. These
  // cachelines holds the hottest fields accessed during hot code, i.e.
  // getIoBuf()
  static_assert(
      sizeof(IoUringDynamicProvidedBufferRing) ==
      2 * folly::hardware_constructive_interference_size);

  static_assert(
      alignof(IoUringDynamicProvidedBufferRing) ==
      folly::hardware_constructive_interference_size);

  static_assert(
      sizeof(folly::DistributedMutex) == 8,
      "folly::DistributedMutex size changed from 8 bytes");
}

IoUringDynamicProvidedBufferRing::UniquePtr
IoUringDynamicProvidedBufferRing::create(io_uring* ioRingPtr, Options options) {
  return IoUringDynamicProvidedBufferRing::UniquePtr(
      new IoUringDynamicProvidedBufferRing(ioRingPtr, options));
}

void IoUringDynamicProvidedBufferRing::mapRing() {
  uint32_t ringMemSize = sizeof(struct io_uring_buf) * ringBufferCount_;
  ringMemSize_ =
      folly::to_narrow(folly::align_ceil(ringMemSize, kBufferAlignBytes));

  ringMem_ = ::mmap(
      nullptr,
      ringMemSize_,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);

  if (ringMem_ == MAP_FAILED) {
    auto errnoCopy = errno;
    throw std::runtime_error(
        folly::to<std::string>(
            "unable to allocate ring memory of size ",
            ringMemSize_,
            ": ",
            folly::errnoStr(errnoCopy)));
  }

  ringPtr_ = static_cast<struct io_uring_buf_ring*>(ringMem_);
}

std::unique_ptr<IoUringDynamicProvidedBufferRing::BufferArea>
IoUringDynamicProvidedBufferRing::createArea() noexcept {
  size_t memSize = static_cast<size_t>(ringBufferCount_) * sizePerBuffer_;
  memSize = folly::align_ceil(memSize, kHugePageSizeBytes);

  void* mem = ::mmap(
      nullptr,
      memSize,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);

  if (mem == MAP_FAILED) {
    PLOG(ERROR) << "unable to allocate area block of size " << memSize;
    return nullptr;
  }

  int ret = ::madvise(mem, memSize, MADV_HUGEPAGE);
  PLOG_IF(ERROR, ret) << "cannot enable huge pages";

  try {
    auto area = std::make_unique<BufferArea>();
    area->memSize = memSize;
    area->buffers = static_cast<char*>(mem);
    area->states = std::make_unique<BufferState[]>(ringBufferCount_);
    for (uint16_t b = 0; b < ringBufferCount_; b++) {
      area->states[b].area = area.get();
      area->states[b].bufId = b;
      area->states[b].parent = this;
      area->states[b].offset = 0;
    }
    return area;
  } catch (const std::bad_alloc& ex) {
    ::munmap(mem, memSize);
    LOG(ERROR) << "unable to allocate area metadata: " << ex.what();
    return nullptr;
  }
}

IoUringDynamicProvidedBufferRing::BufferArea*
IoUringDynamicProvidedBufferRing::addArea() noexcept {
  if (areaCount_ == std::numeric_limits<decltype(areaCount_)>::max()) {
    LOG(ERROR) << "cannot add area: areaCount_ would exceed "
               << std::numeric_limits<decltype(areaCount_)>::max();
    return nullptr;
  }

  auto area = createArea();
  if (area == nullptr) {
    return nullptr;
  }

  BufferArea* areaPtr = area.get();
  areas_.push_back(std::move(area));
  areaCount_++;
  return areaPtr;
}

IoUringDynamicProvidedBufferRing::IoUringDynamicProvidedBufferRing(
    io_uring* ioRingPtr, Options options)
    : sizePerBuffer_(std::max(options.bufferSize, kMinBufferSize)),
      ringBufferCount_(options.bufferCount),
      gid_(options.gid),
      ringIoPtr(ioRingPtr),
      useIncremental_(options.useIncrementalBuffers) {
  if (ringBufferCount_ > kMaxRingRefillEntries) {
    throw std::runtime_error(
        folly::to<std::string>(
            "bufferCount cannot be larger than ", kMaxRingRefillEntries));
  }
  if (ringBufferCount_ == 0) {
    throw std::runtime_error("bufferCount cannot be 0");
  }
  if (ringBufferCount_ < 2) {
    LOG(WARNING) << folly::to<std::string>(
        "Buffer count is too small, adjusting to 2");
    ringBufferCount_ = 2;
  }
  if (!folly::isPowTwo(ringBufferCount_)) {
    auto adjustedBufferCount = folly::nextPowTwo(ringBufferCount_);
    LOG(WARNING) << folly::to<std::string>(
        "Buffer count isn't a power of 2: ",
        ringBufferCount_,
        ", adjusting to: ",
        adjustedBufferCount);
    ringBufferCount_ = adjustedBufferCount;
  }

  areas_.reserve(kMaxAreaCount);

  mapRing();
  initialRegister();

  for (uint32_t i = 0; i < kInitialAreaCount; i++) {
    if (addArea() == nullptr) {
      throw std::runtime_error(
          "unable to allocate initial provided buffer areas");
    }
  }

  bufferActiveArea_ = areas_[0].get();
  bufferRefillArea_ = areas_[0].get();

  ringRefill();
}

void IoUringDynamicProvidedBufferRing::enobuf() noexcept {
  ringRefill();
  if (ringFillLevel() > 0) {
    return;
  }
  if (!enobuf_) {
    enobuf_ = true;
    enobufCount_++;
  }
}

uint32_t IoUringDynamicProvidedBufferRing::getAndResetEnobufCount() noexcept {
  auto count = enobufCount_;
  enobufCount_ = 0;
  return count;
}

void IoUringDynamicProvidedBufferRing::destroy() noexcept {
  std::unique_lock lock{mutex_};
  ::io_uring_unregister_buf_ring(ringIoPtr, gid_);
  DCHECK(bufferGetCount_ >= bufferReturnedCount);
  auto remaining = bufferGetCount_ - bufferReturnedCount;
  shutdownReferences_ = remaining;
  wantsShutdown_ = true;
  lock.unlock();
  delayedDestroy(remaining);
}

bool IoUringDynamicProvidedBufferRing::getNewRefillArea() noexcept {
  // areas_ are sorted so that recently used area at move to the end of the
  // vector. This allows to keep the potentially free areas to the front
  auto drained =
      std::find_if(areas_.begin(), areas_.end(), [this](const auto& area) {
        return areaIsDrained(*area);
      });
  if (drained != areas_.end()) {
    auto area = drained->get();
    DCHECK(area != bufferActiveArea_ && area != bufferRefillArea_)
        << "active and refill areas must not be drained";
    bufferRefillArea_ = area;
    std::rotate(drained, drained + 1, areas_.end());
    return true;
  }

  if (areaCount_ >= kMaxAreaCount) {
    return false;
  }

  // No drained area available and cap not reached. Grow the pool by one and
  // refill from it.
  auto newArea = addArea();
  if (newArea == nullptr) {
    return false;
  }
  bufferRefillArea_ = newArea;
  return true;
}

void IoUringDynamicProvidedBufferRing::ringRefill() noexcept {
  auto startTail = ringTail_;

  // ringIndex(ringTail_) == 0 means we are at an area boundary: either the
  // previous ringRefill consumed all of the refill area, or we are using a
  // newly allocated area. In the former case grab a new refill area.
  if (ringIndex(ringTail_) == 0 && !areaIsDrained(*bufferRefillArea_) &&
      !getNewRefillArea()) {
    return;
  }

  uint16_t pendingOutstanding = 0;
  auto freeEntries = ringFreeEntries();
  while (freeEntries--) {
    uint16_t bid = ringIndex(ringTail_);
    if (useIncremental_) {
      bufferRefillArea_->states[bid].offset = 0;
      bufferRefillArea_->states[bid].refCount.store(1);
    }

    auto* r = ringBuf(ringTail_);
    ringTail_++;
    r->addr = reinterpret_cast<__u64>(getData(*bufferRefillArea_, bid));
    r->len = sizePerBuffer_;
    r->bid = bid;
    pendingOutstanding++;

    // We reached the end of the refill area, flush the pendingOutstanding
    // counter at once and get a new area if there are still freeEntries to
    // refill
    if (ringIndex(ringTail_) == 0) {
      bufferRefillArea_->outstanding.fetch_add(
          pendingOutstanding, std::memory_order_relaxed);
      pendingOutstanding = 0;
      if (freeEntries && !getNewRefillArea()) {
        break;
      }
    }
  }
  if (pendingOutstanding > 0) {
    bufferRefillArea_->outstanding.fetch_add(
        pendingOutstanding, std::memory_order_relaxed);
  }

  // If we refilled entries, then try to update the shared tail
  if (ringTail_ != startTail) {
    if (tryPublish(startTail, ringTail_)) {
      enobuf_ = false;
    }
  }
}

void IoUringDynamicProvidedBufferRing::bufFreeFn(
    void* /* buf */, void* userData) noexcept {
  auto* bufferState = static_cast<BufferState*>(userData);
  IoUringDynamicProvidedBufferRing* parent = bufferState->parent;
  uint16_t bufId = bufferState->bufId;
  BufferArea* area = bufferState->area;
  parent->decBufferState(*area, bufId);
}

std::unique_ptr<IOBuf> IoUringDynamicProvidedBufferRing::getIoBufSingle(
    uint16_t bid, size_t length, bool hasMore) noexcept {
  std::unique_ptr<IOBuf> ret;
  DCHECK(!wantsShutdown_);
  DCHECK_LT(bid, ringBufferCount_)
      << "Buffer index " << bid << " exceeds buffer count " << ringBufferCount_;

  auto* bufferStart = getData(*bufferActiveArea_, bid);
  BufferState* info = &bufferActiveArea_->states[bid];
  if (useIncremental_) {
    unsigned int currentOffset = info->offset;
    auto* dataPtr = bufferStart + currentOffset;
    ret = IOBuf::takeOwnership(
        static_cast<void*>(dataPtr), length, length, bufFreeFn, info);
  } else {
    ret = IOBuf::takeOwnership(
        static_cast<void*>(bufferStart),
        sizePerBuffer_,
        length,
        bufFreeFn,
        info);
  }

  ret->markExternallySharedOne();
  incBufferState(*bufferActiveArea_, bid, hasMore, length);

  ringRefill();
  return ret;
}

std::unique_ptr<IOBuf> IoUringDynamicProvidedBufferRing::getIoBuf(
    uint16_t startBufId, size_t totalLength, bool hasMore) noexcept {
  DCHECK_EQ(ringBuf(ringHead_)->bid, startBufId)
      << "ringHead_ out of sync with kernel ring head";

  size_t currentAvailable = sizePerBuffer_;
  if (useIncremental_) {
    currentAvailable -= bufferActiveArea_->states[startBufId].offset;
  }
  if (totalLength <= currentAvailable) {
    return getIoBufSingle(startBufId, totalLength, hasMore);
  }

  std::unique_ptr<IOBuf> head;
  size_t remainingLength = totalLength;
  uint16_t bid = startBufId;

  while (remainingLength > 0) {
    DCHECK_LT(bid, ringBufferCount_)
        << "Buffer index " << bid << " exceeds buffer count "
        << ringBufferCount_;

    BufferState* bufferState = &bufferActiveArea_->states[bid];
    char* bufferStart = getData(*bufferActiveArea_, bid);
    unsigned int currentOffset = 0;
    size_t availableInBuffer = sizePerBuffer_;

    if (useIncremental_) {
      currentOffset = bufferState->offset;
      availableInBuffer = sizePerBuffer_ - currentOffset;
    }

    char* dataPtr = bufferStart + currentOffset;
    size_t currentChunkSize = std::min(remainingLength, availableInBuffer);
    bool isLastChunk = (remainingLength <= availableInBuffer);

    std::unique_ptr<IOBuf> chunk;
    chunk = IOBuf::takeOwnership(
        static_cast<void*>(dataPtr),
        useIncremental_ ? currentChunkSize : sizePerBuffer_,
        currentChunkSize,
        bufFreeFn,
        bufferState);

    chunk->markExternallySharedOne();
    if (!head) {
      head = std::move(chunk);
    } else {
      head->appendToChain(std::move(chunk));
    }

    bool bufHasMore = hasMore && isLastChunk;
    incBufferState(*bufferActiveArea_, bid, bufHasMore, currentChunkSize);
    remainingLength -= currentChunkSize;

    bid = ringIndex(bid + 1);
  }

  ringRefill();
  return head;
}

std::unique_ptr<IOBuf> IoUringDynamicProvidedBufferRing::getIoBuf(
    const struct io_uring_cqe* cqe) noexcept {
  auto bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
  auto res = cqe->res;
  bool hasMore = (cqe->flags & IORING_CQE_F_BUF_MORE) != 0;
  return getIoBuf(bid, res, hasMore);
}

void IoUringDynamicProvidedBufferRing::initialRegister() {
  struct io_uring_buf_reg reg{};
  memset(&reg, 0, sizeof(reg));
  reg.ring_addr = reinterpret_cast<__u64>(ringPtr_);
  reg.ring_entries = ringBufferCount_;
  reg.bgid = gid_;

  int flags = useIncremental_ ? IOU_PBUF_RING_INC : 0;
  int ret = ::io_uring_register_buf_ring(ringIoPtr, &reg, flags);

  if (ret) {
    LOG(ERROR) << folly::to<std::string>(
        "unable to register provided buffer ring ",
        -ret,
        ": ",
        folly::errnoStr(-ret));
    LOG(ERROR) << folly::to<std::string>(
        "buffer ring buffer count: ",
        ringBufferCount_,
        ", size per buf: ",
        sizePerBuffer_,
        ", bgid: ",
        gid_);
    throw LibUringCallError("unable to register provided buffer ring");
  }
}

void IoUringDynamicProvidedBufferRing::delayedDestroy(uint32_t refs) noexcept {
  if (refs == 0) {
    ::munmap(ringMem_, ringMemSize_);
    for (auto& area : areas_) {
      ::munmap(area->buffers, area->memSize);
    }
    delete this;
  }
}

void IoUringDynamicProvidedBufferRing::incBufferState(
    BufferArea& area,
    uint16_t bid,
    bool hasMore,
    size_t bytesConsumed) noexcept {
  bufferGetCount_++;

  if (useIncremental_ && hasMore) {
    BufferState* bufferState = &area.states[bid];
    bufferState->refCount.fetch_add(1);
    bufferState->offset += bytesConsumed;
  }

  if (!hasMore) {
    ringHead_++;

    // We consumed all the buffers of the current area. active area becomes the
    // refill area
    if (bid == ringBufferCount_ - 1) {
      bufferActiveArea_ = bufferRefillArea_;
    }
  }
}

void IoUringDynamicProvidedBufferRing::decBufferState(
    BufferArea& area, uint16_t bid) noexcept {
  std::unique_lock lock{mutex_};
  bufferReturnedCount++;

  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    auto refs = --shutdownReferences_;
    lock.unlock();
    delayedDestroy(refs);
    return;
  }

  if (!useIncremental_) {
    area.outstanding.fetch_sub(1, std::memory_order_release);
    return;
  }

  auto oldRefCount = area.states[bid].refCount.fetch_sub(1);
  if (oldRefCount == 1) {
    area.outstanding.fetch_sub(1, std::memory_order_release);
  }
}

int IoUringDynamicProvidedBufferRing::getUtilPct() const noexcept {
  uint64_t totalBuffers = areaCount_ * ringBufferCount_;
  auto outstanding = areasOutstandingSum();
  auto inRing = ringFillLevel();
  uint64_t inUse = (outstanding > inRing) ? (outstanding - inRing) : 0;
  inUse = std::min(inUse, totalBuffers);

  return (100 * inUse) / totalBuffers;
}

} // namespace folly

#endif
