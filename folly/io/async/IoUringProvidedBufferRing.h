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

#include <folly/io/async/IoUringBase.h>
#include <folly/io/async/Liburing.h>
#include <folly/synchronization/DistributedMutex.h>

#if FOLLY_HAS_LIBURING

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wnested-anon-types")
FOLLY_CLANG_DISABLE_WARNING("-Wzero-length-array")
#include <liburing.h> // @manual
FOLLY_POP_WARNING

namespace folly {

class IoUringProvidedBufferRing {
 public:
  friend class IoUringProvidedBufferRingTestHelper;

  class LibUringCallError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  struct Deleter {
    void operator()(IoUringProvidedBufferRing* ring) {
      if (ring) {
        ring->destroy();
      }
    }
  };

  using UniquePtr = std::unique_ptr<IoUringProvidedBufferRing, Deleter>;

  struct Options {
    uint16_t gid{0};
    size_t bufferCount{0};
    size_t bufferSize{0};
    bool useHugePages{false};
    bool useIncrementalBuffers{false};
  };

  static UniquePtr create(io_uring* ioRingPtr, Options options);

  ~IoUringProvidedBufferRing() = default;

  void enobuf() noexcept;
  uint64_t getAndResetEnobufCount() noexcept;
  void destroy() noexcept;

  std::unique_ptr<IOBuf> getIoBuf(
      uint16_t startBufId, size_t totalLength, bool hasMore) noexcept;

  uint32_t count() const noexcept { return buffer_.bufferCount(); }
  bool available() const noexcept {
    return !enobuf_.load(std::memory_order_relaxed);
  }

  size_t sizePerBuffer() const noexcept { return sizePerBuffer_; }
  uint16_t gid() const noexcept { return gid_; }

  // Returns the buffer utilization as an integer percentage (0-100).
  int getUtilPct() const noexcept;

 private:
  explicit IoUringProvidedBufferRing(io_uring* ioRingPtr, Options options);

  IoUringProvidedBufferRing(IoUringProvidedBufferRing&&) = delete;
  IoUringProvidedBufferRing(IoUringProvidedBufferRing const&) = delete;
  IoUringProvidedBufferRing& operator=(IoUringProvidedBufferRing&&) = delete;
  IoUringProvidedBufferRing& operator=(IoUringProvidedBufferRing const&) =
      delete;

  void initialRegister();
  void returnBuffer(uint16_t i) noexcept;
  void delayedDestroy(uint64_t refs) noexcept;
  void incBufferState(
      uint16_t bufId, bool hasMore, unsigned int bytesConsumed) noexcept;
  void decBufferState(uint16_t bufId) noexcept;
  std::unique_ptr<IOBuf> getIoBufSingle(
      uint16_t i, size_t length, bool hasMore) noexcept;

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
        size_t bufferCount, size_t bufferSize, bool useHugePages);
    ~ProvidedBuffersBuffer();

    struct io_uring_buf_ring* ring() const noexcept { return ringPtr_; }

    struct io_uring_buf* ringBuf(int idx) const noexcept {
      return &ringPtr_->bufs[idx & ringMask_];
    }

    uint32_t bufferCount() const noexcept { return bufferCount_; }
    uint32_t ringCount() const noexcept { return 1 + ringMask_; }

    char* buffer(uint16_t idx) {
      size_t offset = (size_t)idx * sizePerBuffer_;
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
    size_t sizePerBuffer_;
    char* bufferBuffer_;
    uint32_t bufferCount_;
  };

  struct BufferState {
    uint16_t bufId{0};
    // Starting with a refCount of 1, to account for moreData incoming
    // in the incremental buffer case.
    std::atomic<uint32_t> refCount{1};
    unsigned int offset{0};
    IoUringProvidedBufferRing* parent{nullptr};
  };

  uint16_t const gid_;
  size_t const sizePerBuffer_;
  io_uring* ioRingPtr_;
  ProvidedBuffersBuffer buffer_;
  std::atomic<bool> enobuf_{false};
  std::atomic<uint64_t> enobufCount_{0};
  bool useIncremental_;

  // For tracking how many IOBufs were created
  uint64_t gottenBuffers_{0};
  // For tracking how many IOBufs were destroyed.
  uint64_t returnedBuffers_{0};
  // For returning the buffer to the ring.
  uint64_t ringReturnedBuffers_{0};
  std::unique_ptr<BufferState[]> bufferStates_;

  folly::DistributedMutex mutex_;
  std::atomic<bool> wantsShutdown_{false};
  uint64_t shutdownReferences_{0};
};

} // namespace folly

#endif
