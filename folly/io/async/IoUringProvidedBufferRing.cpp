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

#include <folly/io/async/IoUringProvidedBufferRing.h>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/lang/Align.h>
#include <folly/portability/SysMman.h>

#if FOLLY_HAS_LIBURING

namespace {
constexpr uint32_t kMinBufferSize = 32;
constexpr uint32_t kHugePageSizeBytes = 1024 * 1024 * 2;
constexpr uint32_t kPageSizeBytes = 4096;
constexpr uint32_t kBufferAlignBytes = 32;
} // namespace

namespace folly {

void IoUringProvidedBufferRing::checkInvariants() {
  // This object is carefully packed into two 64 byte cache lines. The first
  // cache line contains all of the fields accessed during hot code, i.e.
  // getIoBuf() and returnBuffer(). The second cache line contains all the warm
  // and cold fields that are rarely accessed.
  static_assert(
      sizeof(IoUringProvidedBufferRing) ==
      2 * folly::hardware_constructive_interference_size);

  static_assert(
      alignof(IoUringProvidedBufferRing) ==
      folly::hardware_constructive_interference_size);

  static_assert(
      sizeof(folly::DistributedMutex) == 8,
      "folly::DistributedMutex size changed from 8 bytes");
}

IoUringProvidedBufferRing::UniquePtr IoUringProvidedBufferRing::create(
    io_uring* ioRingPtr, Options options) {
  return IoUringProvidedBufferRing::UniquePtr(
      new IoUringProvidedBufferRing(ioRingPtr, options));
}

void IoUringProvidedBufferRing::mapMemory(bool useHugePages) {
  // Find next power of 2 size larger than bufferCount for the provided buffer
  // ring count.
  int ringShift = folly::findLastSet(bufferCount_) - 1;
  if (bufferCount_ != (1ULL << ringShift)) {
    ringShift++;
  }
  ringCount_ = 1U << std::max<int>(ringShift, 1);
  ringMask_ = ringCount_ - 1;
  ringMemSize_ = sizeof(struct io_uring_buf) * ringCount_;
  ringMemSize_ =
      folly::to_narrow(folly::align_ceil(ringMemSize_, kBufferAlignBytes));

  bufferSize_ = sizePerBuffer_ * bufferCount_;
  allSize_ = ringMemSize_ + bufferSize_;

  int pages;
  if (useHugePages) {
    allSize_ =
        folly::to_narrow(folly::align_ceil(allSize_, kHugePageSizeBytes));
    pages = allSize_ / kHugePageSizeBytes;
  } else {
    allSize_ = folly::to_narrow(folly::align_ceil(allSize_, kPageSizeBytes));
    pages = allSize_ / kPageSizeBytes;
  }

  buffer_ = ::mmap(
      nullptr,
      allSize_,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0);

  if (buffer_ == MAP_FAILED) {
    auto errnoCopy = errno;
    throw std::runtime_error(
        folly::to<std::string>(
            "unable to allocate pages of size ",
            allSize_,
            " pages=",
            pages,
            ": ",
            folly::errnoStr(errnoCopy)));
  }

  ringPtr_ = static_cast<struct io_uring_buf_ring*>(buffer_);
  bufferBuffer_ = static_cast<char*>(buffer_) + ringMemSize_;

  if (useHugePages) {
    int ret = ::madvise(buffer_, allSize_, MADV_HUGEPAGE);
    PLOG_IF(ERROR, ret) << "cannot enable huge pages";
  } else {
    ::madvise(buffer_, allSize_, MADV_NOHUGEPAGE);
  }
}

IoUringProvidedBufferRing::IoUringProvidedBufferRing(
    io_uring* ioRingPtr, Options options)
    : bufferStates_(),
      sizePerBuffer_(std::max(options.bufferSize, kMinBufferSize)),
      bufferCount_(options.bufferCount),
      useIncremental_(options.useIncrementalBuffers),
      ioRingPtr_(ioRingPtr),
      gid_(options.gid) {
  if (bufferCount_ > std::numeric_limits<uint16_t>::max()) {
    throw std::runtime_error("bufferCount cannot be larger than 65,535");
  }
  if (bufferCount_ == 0) {
    throw std::runtime_error("bufferCount cannot be 0");
  }

  mapMemory(options.useHugePages);
  initialRegister();

  bufferStates_ = std::make_unique<BufferState[]>(bufferCount_);
  for (uint16_t i = 0; i < bufferCount_; i++) {
    bufferStates_[i].bufId = i;
    bufferStates_[i].parent = this;
    bufferStates_[i].offset = 0;
  }

  for (size_t i = 0; i < bufferCount_; i++) {
    returnBuffer(i);
  }
}

void IoUringProvidedBufferRing::enobuf() noexcept {
  {
    // what we want to do is something like
    // if (cachedTail_ != localTail_) {
    //   publish();
    //   enobuf_ = false;
    // }
    // but if we are processing a batch it doesn't really work
    // because we'll likely get an ENOBUF straight after
    enobuf_.store(true, std::memory_order_relaxed);
    enobufCount_.fetch_add(1, std::memory_order_relaxed);
  }
}

uint32_t IoUringProvidedBufferRing::getAndResetEnobufCount() noexcept {
  return enobufCount_.exchange(0, std::memory_order_relaxed);
}

void IoUringProvidedBufferRing::destroy() noexcept {
  std::unique_lock lock{mutex_};
  ::io_uring_unregister_buf_ring(ioRingPtr_, gid());
  DCHECK(gottenBuffers_ >= returnedBuffers_);
  auto remaining = gottenBuffers_ - returnedBuffers_;
  shutdownReferences_ = remaining;
  wantsShutdown_ = true;
  lock.unlock();
  delayedDestroy(remaining);
}

void IoUringProvidedBufferRing::returnBuffer(uint16_t i) noexcept {
  std::unique_lock lock{mutex_};
  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    auto refs = --shutdownReferences_;
    lock.unlock();
    delayedDestroy(refs);
    return;
  }

  if (useIncremental_) {
    bufferStates_[i].offset = 0;
    bufferStates_[i].refCount.store(1);
  }

  uint16_t this_idx = static_cast<uint16_t>(ringReturnedBuffers_++);
  uint16_t next_tail = this_idx + 1;

  auto* r = ringBuf(this_idx);
  r->addr = reinterpret_cast<__u64>(getData(i));
  r->len = sizePerBuffer_;
  r->bid = i;

  if (tryPublish(this_idx, next_tail)) {
    enobuf_.store(false, std::memory_order_relaxed);
  }
}

std::unique_ptr<IOBuf> IoUringProvidedBufferRing::getIoBufSingle(
    uint16_t i, size_t length, bool hasMore) noexcept {
  std::unique_ptr<IOBuf> ret;
  DCHECK(!wantsShutdown_);
  DCHECK_LT(i, bufferCount_)
      << "Buffer index " << i << " exceeds buffer count " << bufferCount_;

  auto free_fn = [](void*, void* userData) {
    auto* bufferState = static_cast<BufferState*>(userData);
    IoUringProvidedBufferRing* parent = bufferState->parent;
    uint16_t bufId = bufferState->bufId;
    parent->decBufferState(bufId);
  };

  if (useIncremental_) {
    auto* bufferStart = getData(i);
    unsigned int currentOffset = bufferStates_[i].offset;
    auto* dataPtr = bufferStart + currentOffset;
    BufferState* info = &bufferStates_[i];
    ret = IOBuf::takeOwnership(
        static_cast<void*>(dataPtr), length, length, free_fn, info);
  } else {
    BufferState* info = &bufferStates_[i];
    ret = IOBuf::takeOwnership(
        static_cast<void*>(getData(i)), sizePerBuffer_, length, free_fn, info);
  }

  ret->markExternallySharedOne();
  incBufferState(i, hasMore, length);

  return ret;
}

std::unique_ptr<IOBuf> IoUringProvidedBufferRing::getIoBuf(
    uint16_t startBufId, size_t totalLength, bool hasMore) noexcept {
  if (totalLength <= sizePerBuffer_) {
    return getIoBufSingle(startBufId, totalLength, hasMore);
  }

  auto free_fn = [](void*, void* userData) {
    auto* bufferState = static_cast<BufferState*>(userData);
    IoUringProvidedBufferRing* parent = bufferState->parent;
    uint16_t bufId = bufferState->bufId;
    parent->decBufferState(bufId);
  };

  std::unique_ptr<IOBuf> head;
  size_t remainingLength = totalLength;
  uint16_t currentBufId = startBufId;

  while (remainingLength > 0) {
    DCHECK_LT(currentBufId, bufferCount_)
        << "Buffer index " << currentBufId << " exceeds buffer count "
        << bufferCount_;

    BufferState* bufferState = &bufferStates_[currentBufId];
    char* bufferStart = getData(currentBufId);
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
        free_fn,
        bufferState);

    chunk->markExternallySharedOne();
    if (!head) {
      head = std::move(chunk);
    } else {
      head->appendToChain(std::move(chunk));
    }

    incBufferState(currentBufId, hasMore && isLastChunk, currentChunkSize);
    remainingLength -= currentChunkSize;
    currentBufId = (currentBufId + 1) & (bufferCount_ - 1);
  }

  return head;
}

std::unique_ptr<IOBuf> IoUringProvidedBufferRing::getIoBuf(
    const struct io_uring_cqe* cqe) noexcept {
  auto bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
  auto res = cqe->res;
  bool hasMore = (cqe->flags & IORING_CQE_F_BUF_MORE) != 0;
  return getIoBuf(bid, res, hasMore);
}

void IoUringProvidedBufferRing::initialRegister() {
  struct io_uring_buf_reg reg{};
  memset(&reg, 0, sizeof(reg));
  reg.ring_addr = reinterpret_cast<__u64>(ringPtr_);
  reg.ring_entries = ringCount_;
  reg.bgid = gid_;

  int flags = useIncremental_ ? IOU_PBUF_RING_INC : 0;
  int ret = ::io_uring_register_buf_ring(ioRingPtr_, &reg, flags);

  if (ret) {
    LOG(ERROR) << folly::to<std::string>(
        "unable to register provided buffer ring ",
        -ret,
        ": ",
        folly::errnoStr(-ret));
    LOG(ERROR) << folly::to<std::string>(
        "buffer ring buffer count: ",
        bufferCount_,
        ", ring count: ",
        ringCount_,
        ", size per buf: ",
        sizePerBuffer_,
        ", bgid: ",
        gid_);
    throw LibUringCallError("unable to register provided buffer ring");
  }
}

void IoUringProvidedBufferRing::delayedDestroy(uint32_t refs) noexcept {
  if (refs == 0) {
    ::munmap(buffer_, allSize_);
    delete this;
  }
}

void IoUringProvidedBufferRing::incBufferState(
    uint16_t bufId, bool hasMore, size_t bytesConsumed) noexcept {
  gottenBuffers_++;

  if (useIncremental_ && hasMore) {
    bufferStates_[bufId].refCount.fetch_add(1);
    bufferStates_[bufId].offset += bytesConsumed;
  }

  // No need to handle regular buffers, since it is never really
  // incremented beyond the original assingment of 1 instead of 0.
}

void IoUringProvidedBufferRing::decBufferState(uint16_t bufId) noexcept {
  returnedBuffers_++;

  if (!useIncremental_ || wantsShutdown_) {
    returnBuffer(bufId);
    return;
  }

  uint16_t oldRefCount = bufferStates_[bufId].refCount.fetch_sub(1);
  if (oldRefCount == 1) {
    returnBuffer(bufId);
  }
}

int IoUringProvidedBufferRing::getUtilPct() const noexcept {
  uint32_t totalBuffers = bufferCount_;
  uint16_t head = 0;
  int ret = ::io_uring_buf_ring_head(ioRingPtr_, gid(), &head);
  if (ret != 0) {
    return ret;
  }
  // Use ring mask to extract ring position from wrapped uint16_t counters
  // Ring size is power of 2, mask handles wrap-around explicitly
  uint32_t available = (ringPtr_->tail - head) & ringMask_;
  available = std::min(available, totalBuffers);

  uint32_t inUse = totalBuffers - available;
  return (100 * inUse) / totalBuffers;
}

} // namespace folly

#endif
