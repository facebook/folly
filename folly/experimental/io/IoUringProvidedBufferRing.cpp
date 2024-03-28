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

#include <folly/experimental/io/IoUringProvidedBufferRing.h>

#include <folly/Conv.h>
#include <folly/ExceptionString.h>
#include <folly/String.h>

#if FOLLY_HAS_LIBURING

namespace folly {

IoUringProvidedBufferRing::ProvidedBuffersBuffer::ProvidedBuffersBuffer(
    int count, int bufferShift, int ringCountShift, bool huge_pages)
    : bufferShift_(bufferShift), bufferCount_(count) {
  // space for the ring
  int ringCount = 1 << ringCountShift;
  ringMask_ = ringCount - 1;
  ringMemSize_ = sizeof(struct io_uring_buf) * ringCount;

  ringMemSize_ = (ringMemSize_ + kBufferAlignMask) & (~kBufferAlignMask);

  if (bufferShift_ < 5) {
    bufferShift_ = 5; // for alignment
  }

  sizePerBuffer_ = calcBufferSize(bufferShift_);
  bufferSize_ = sizePerBuffer_ * count;
  allSize_ = ringMemSize_ + bufferSize_;

  int pages;
  if (huge_pages) {
    allSize_ = (allSize_ + kHugePageMask) & (~kHugePageMask);
    pages = allSize_ / (1 + kHugePageMask);
  } else {
    allSize_ = (kPageMask + kPageMask) & ~kPageMask;
    pages = allSize_ / (1 + kPageMask);
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
    int ret = madvise(buffer_, allSize_, MADV_HUGEPAGE);
    PLOG_IF(ERROR, ret) << "cannot enable huge pages";
  }
}

IoUringProvidedBufferRing::IoUringProvidedBufferRing(
    io_uring* ioRingPtr,
    uint16_t gid,
    int count,
    int bufferShift,
    int ringSizeShift)
    : IoUringBufferProviderBase(
          gid, ProvidedBuffersBuffer::calcBufferSize(bufferShift)),
      ioRingPtr_(ioRingPtr),
      buffer_(count, bufferShift, ringSizeShift, true) {
  if (count > std::numeric_limits<uint16_t>::max()) {
    throw std::runtime_error("too many buffers");
  }
  if (count <= 0) {
    throw std::runtime_error("not enough buffers");
  }

  ioBufCallbacks_.assign((count + (sizeof(void*) - 1)) / sizeof(void*), this);

  initialRegister();

  gottenBuffers_ += count;
  for (int i = 0; i < count; i++) {
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
  ::io_uring_unregister_buf_ring(ioRingPtr_, gid());
  shutdownReferences_ = 1;
  auto returned = returnedBuffers_.load();
  {
    std::lock_guard<std::mutex> guard(shutdownMutex_);
    wantsShutdown_ = true;
    // add references for every missing one
    // we can assume that there will be no more from the kernel side.
    // there is a race condition here between reading wantsShutdown_ and
    // a return incrementing the number of returned references, but it is very
    // unlikely to trigger as everything is shutting down, so there should not
    // actually be any buffer returns. Worse case this will leak, but since
    // everything is shutting down anyway it should not be a problem.
    uint64_t const gotten = gottenBuffers_;
    DCHECK(gottenBuffers_ >= returned);
    uint32_t outstanding = (gotten - returned);
    shutdownReferences_ += outstanding;
  }
  if (shutdownReferences_.fetch_sub(1) == 1) {
    delete this;
  }
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
    throw LibUringCallError(folly::to<std::string>(
        "unable to register provided buffer ring ",
        -ret,
        ": ",
        folly::errnoStr(-ret)));
  }
}

void IoUringProvidedBufferRing::returnBufferInShutdown() noexcept {
  { std::lock_guard<std::mutex> guard(shutdownMutex_); }
  if (shutdownReferences_.fetch_sub(1) == 1) {
    delete this;
  }
}

void IoUringProvidedBufferRing::returnBuffer(uint16_t i) noexcept {
  if (FOLLY_UNLIKELY(wantsShutdown_)) {
    returnBufferInShutdown();
    return;
  }
  uint16_t this_idx = static_cast<uint16_t>(returnedBuffers_++);
  __u64 addr = (__u64)buffer_.buffer(i);
  uint16_t next_tail = this_idx + 1;
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
