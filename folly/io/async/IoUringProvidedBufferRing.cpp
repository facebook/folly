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

#if FOLLY_HAS_LIBURING

namespace folly {

IoUringBufferProviderBase::UniquePtr IoUringProvidedBufferRing::create(
    io_uring* ioRingPtr, Options options) {
  return IoUringProvidedBufferRing::UniquePtr(
      new IoUringProvidedBufferRing(ioRingPtr, options));
}

IoUringProvidedBufferRing::ProvidedBuffersBuffer::ProvidedBuffersBuffer(
    size_t count, int bufferShift, int ringCountShift, bool huge_pages)
    : bufferShift_(bufferShift), bufferCount_(count) {
  // space for the ring
  int ringCount = 1 << ringCountShift;
  ringMask_ = ringCount - 1;
  ringMemSize_ = sizeof(struct io_uring_buf) * ringCount;

  ringMemSize_ = align_ceil(ringMemSize_, kBufferAlignBytes);

  if (bufferShift_ < 5) {
    bufferShift_ = 5; // for alignment
  }

  sizePerBuffer_ = calcBufferSize(bufferShift_);
  bufferSize_ = sizePerBuffer_ * count;
  allSize_ = ringMemSize_ + bufferSize_;

  int pages;
  if (huge_pages) {
    allSize_ = align_ceil(allSize_, kHugePageSizeBytes);
    pages = allSize_ / kHugePageSizeBytes;
  } else {
    allSize_ = align_ceil(allSize_, kPageSizeBytes);
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
    throw std::runtime_error(folly::to<std::string>(
        "unable to allocate pages of size ",
        allSize_,
        " pages=",
        pages,
        ": ",
        folly::errnoStr(errnoCopy)));
  }

  bufferBuffer_ = ((char*)buffer_) + ringMemSize_;
  ringPtr_ = (struct io_uring_buf_ring*)buffer_;

  if (huge_pages) {
    int ret = ::madvise(buffer_, allSize_, MADV_HUGEPAGE);
    PLOG_IF(ERROR, ret) << "cannot enable huge pages";
  } else {
    ::madvise(buffer_, allSize_, MADV_NOHUGEPAGE);
  }
}

IoUringProvidedBufferRing::IoUringProvidedBufferRing(
    io_uring* ioRingPtr, Options options)
    : IoUringBufferProviderBase(
          options.gid,
          ProvidedBuffersBuffer::calcBufferSize(options.bufferShift)),
      ioRingPtr_(ioRingPtr),
      buffer_(
          options.count,
          options.bufferShift,
          options.ringSizeShift,
          options.useHugePages) {
  if (options.count > std::numeric_limits<uint16_t>::max()) {
    throw std::runtime_error("too many buffers");
  }
  if (options.count == 0) {
    throw std::runtime_error("not enough buffers");
  }

  ioBufCallbacks_.assign(
      (options.count + (sizeof(void*) - 1)) / sizeof(void*), this);

  initialRegister();

  gottenBuffers_ += options.count;
  for (size_t i = 0; i < options.count; i++) {
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
  }
  VLOG_EVERY_N(1, 500) << "enobuf";
}

void IoUringProvidedBufferRing::unusedBuf(uint16_t i) noexcept {
  gottenBuffers_++;
  returnBuffer(i);
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

std::unique_ptr<IOBuf> IoUringProvidedBufferRing::getIoBuf(
    uint16_t i, size_t length) noexcept {
  std::unique_ptr<IOBuf> ret;
  DCHECK(!wantsShutdown_);

  // use a weird convention: userData = ioBufCallbacks_.data() + i
  // ioBufCallbacks_ is just a list of the same pointer, to this
  // so we don't need to malloc anything
  auto free_fn = [](void*, void* userData) {
    size_t pprov = (size_t)userData & ~((size_t)(sizeof(void*) - 1));
    IoUringProvidedBufferRing* prov = *(IoUringProvidedBufferRing**)pprov;
    uint16_t idx = (size_t)userData - (size_t)prov->ioBufCallbacks_.data();
    prov->returnBuffer(idx);
  };

  ret = IOBuf::takeOwnership(
      (void*)getData(i),
      sizePerBuffer_,
      length,
      free_fn,
      (void*)(((size_t)ioBufCallbacks_.data()) + i));
  ret->markExternallySharedOne();
  gottenBuffers_++;
  return ret;
}

void IoUringProvidedBufferRing::initialRegister() {
  struct io_uring_buf_reg reg;
  memset(&reg, 0, sizeof(reg));
  reg.ring_addr = (__u64)buffer_.ring();
  reg.ring_entries = buffer_.ringCount();
  reg.bgid = gid();

  int ret = ::io_uring_register_buf_ring(ioRingPtr_, &reg, 0);

  if (ret) {
    LOG(ERROR) << folly::to<std::string>(
        "unable to register provided buffer ring ",
        -ret,
        ": ",
        folly::errnoStr(-ret));
    LOG(ERROR) << folly::to<std::string>(
        "buffer ring buffer count: ",
        buffer_.bufferCount(),
        ", ring count: ",
        buffer_.ringCount(),
        ", size per buf: ",
        buffer_.sizePerBuffer(),
        ", bgid: ",
        gid());
    throw LibUringCallError("unable to register provided buffer ring");
  }
}

void IoUringProvidedBufferRing::delayedDestroy(uint64_t refs) noexcept {
  if (refs == 0) {
    delete this;
  }
}

void IoUringProvidedBufferRing::returnBuffer(uint16_t i) noexcept {
  std::unique_lock lock{mutex_};
  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    auto refs = --shutdownReferences_;
    lock.unlock();
    delayedDestroy(refs);
    return;
  }

  uint16_t this_idx = static_cast<uint16_t>(returnedBuffers_++);
  uint16_t next_tail = this_idx + 1;

  __u64 addr = (__u64)buffer_.buffer(i);
  auto* r = buffer_.ringBuf(this_idx);
  r->addr = addr;
  r->len = buffer_.sizePerBuffer();
  r->bid = i;

  if (tryPublish(this_idx, next_tail)) {
    enobuf_.store(false, std::memory_order_relaxed);
  }
  VLOG(9) << "returnBuffer(" << i << ")@" << this_idx;
}

} // namespace folly

#endif
