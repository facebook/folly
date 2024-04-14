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

#include <folly/experimental/io/IoUringBase.h>
#include <folly/experimental/io/Liburing.h>
#include <folly/portability/SysMman.h>

#if FOLLY_HAS_LIBURING

#include <liburing.h>

namespace folly {

class IoUringProvidedBufferRing : public IoUringBufferProviderBase {
 public:
  class LibUringCallError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  IoUringProvidedBufferRing(
      io_uring* ioRingPtr,
      uint16_t gid,
      int count,
      int bufferShift,
      int ringSizeShift);

  void enobuf() noexcept override;
  void unusedBuf(uint16_t i) noexcept override;
  void destroy() noexcept override;
  std::unique_ptr<IOBuf> getIoBuf(uint16_t i, size_t length) noexcept override;

  uint32_t count() const noexcept override { return buffer_.bufferCount(); }
  bool available() const noexcept override {
    return !enobuf_.load(std::memory_order_relaxed);
  }

 private:
  void initialRegister();
  void returnBufferInShutdown() noexcept;
  void returnBuffer(uint16_t i) noexcept;

  std::atomic<uint16_t>* sharedTail() {
    return reinterpret_cast<std::atomic<uint16_t>*>(&buffer_.ring()->tail);
  }

  bool tryPublish(uint16_t expected, uint16_t value) noexcept {
    return sharedTail()->compare_exchange_strong(
        expected, value, std::memory_order_release);
  }

  char const* getData(uint16_t i) { return buffer_.buffer(i); }

  class ProvidedBuffersBuffer {
   public:
    ProvidedBuffersBuffer(
        int count, int bufferShift, int ringCountShift, bool huge_pages);
    ~ProvidedBuffersBuffer() { ::munmap(buffer_, allSize_); }

    static size_t calcBufferSize(int bufferShift) {
      return 1LLU << std::max<int>(5, bufferShift);
    }

    struct io_uring_buf_ring* ring() const noexcept { return ringPtr_; }

    struct io_uring_buf* ringBuf(int idx) const noexcept {
      return &ringPtr_->bufs[idx & ringMask_];
    }

    uint32_t bufferCount() const noexcept { return bufferCount_; }
    uint32_t ringCount() const noexcept { return 1 + ringMask_; }

    char* buffer(uint16_t idx) {
      size_t offset = (size_t)idx << bufferShift_;
      return bufferBuffer_ + offset;
    }

    size_t sizePerBuffer() const { return sizePerBuffer_; }

   private:
    void* buffer_;
    size_t allSize_;

    size_t ringMemSize_;
    struct io_uring_buf_ring* ringPtr_;
    int ringMask_;

    size_t bufferSize_;
    size_t bufferShift_;
    size_t sizePerBuffer_;
    char* bufferBuffer_;
    uint32_t bufferCount_;

    // static constexpr
    static constexpr size_t kHugePageMask = (1LLU << 21) - 1; // 2MB
    static constexpr size_t kPageMask = (1LLU << 12) - 1; // 4095
    static constexpr size_t kBufferAlignMask{31LLU};
  };

  io_uring* ioRingPtr_;
  ProvidedBuffersBuffer buffer_;
  std::atomic<bool> enobuf_{false};
  std::vector<IoUringProvidedBufferRing*> ioBufCallbacks_;

  uint64_t gottenBuffers_{0};
  std::atomic<uint64_t> returnedBuffers_{0};

  std::atomic<bool> wantsShutdown_{false};
  std::atomic<uint32_t> shutdownReferences_;
  std::mutex shutdownMutex_;
};

} // namespace folly

#endif
