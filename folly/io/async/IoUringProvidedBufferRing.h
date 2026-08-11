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

#include <limits>
#include <vector>

#include <folly/io/async/IoUringBase.h>
#include <folly/io/async/Liburing.h>
#include <folly/synchronization/DistributedMutex.h>

#if FOLLY_HAS_LIBURING

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wnested-anon-types")
FOLLY_CLANG_DISABLE_WARNING("-Wzero-length-array")
FOLLY_GCC_DISABLE_WARNING("-Wshadow")
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
    uint32_t bufferCount{0};
    uint32_t bufferSize{0};
    bool useIncrementalBuffers{false};
  };

  static UniquePtr create(io_uring* ioRingPtr, Options options);

  ~IoUringProvidedBufferRing() = default;

  void enobuf() noexcept;
  uint32_t getAndResetEnobufCount() noexcept;
  void destroy() noexcept;

  std::unique_ptr<IOBuf> getIoBuf(
      uint16_t startBufId, size_t totalLength, bool hasMore) noexcept;
  std::unique_ptr<IOBuf> getIoBuf(const struct io_uring_cqe* cqe) noexcept;

  uint32_t count() const noexcept { return ringBufferCount_; }
  bool available() const noexcept { return !enobuf_; }
  size_t sizePerBuffer() const noexcept { return sizePerBuffer_; }
  uint16_t gid() const noexcept { return gid_; }

  // Returns the buffer utilization as an integer percentage (0-100).
  int getUtilPct() const noexcept;

  uint16_t areaCount() const noexcept { return areaCount_; }

  struct Stats {
    uint32_t enobufCount{0};
    int utilPct{-1};
    uint16_t areaCount{0};

    auto operator<=>(const Stats&) const = default;
  };

  void getStats(Stats& stats) noexcept {
    stats.enobufCount = getAndResetEnobufCount();
    stats.utilPct = getUtilPct();
    stats.areaCount = areaCount_;
  }

 private:
  explicit IoUringProvidedBufferRing(io_uring* ioRingPtr, Options options);

  IoUringProvidedBufferRing(IoUringProvidedBufferRing&&) = delete;
  IoUringProvidedBufferRing(IoUringProvidedBufferRing const&) = delete;
  IoUringProvidedBufferRing& operator=(IoUringProvidedBufferRing&&) = delete;
  IoUringProvidedBufferRing& operator=(IoUringProvidedBufferRing const&) =
      delete;

  void mapRing();
  void initialRegister();
  size_t ringMemSize() const noexcept {
    return sizeof(struct io_uring_buf_ring) * ringBufferCount_;
  }

  struct BufferArea;

  BufferArea* addArea() noexcept;

  void delayedDestroy(uint32_t refs) noexcept;
  void incBufferState(
      BufferArea& area,
      uint16_t bid,
      bool hasMore,
      size_t bytesConsumed) noexcept;
  void decBufferState(BufferArea& area, uint16_t bid) noexcept;

  void ringRefill() noexcept;
  bool getNewRefillArea() noexcept;
  void tryReclaimArea() noexcept;
  std::unique_ptr<IOBuf> getIoBufSingle(
      uint16_t bid, size_t length, bool hasMore) noexcept;

  std::atomic<uint16_t>* sharedTail() {
    return reinterpret_cast<std::atomic<uint16_t>*>(&ringPtr_->tail);
  }

  bool tryPublish(uint16_t expected, uint16_t value) noexcept {
    return sharedTail()->compare_exchange_strong(
        expected, value, std::memory_order_release);
  }

  char* getData(BufferArea& area, uint16_t bid) {
    auto offset = static_cast<size_t>(bid) * sizePerBuffer_;
    return area.buffers + offset;
  }

  struct io_uring_buf* ringBuf(uint32_t idx) const noexcept {
    return &ringPtr_->bufs[ringIndex(idx)];
  }

  uint32_t ringIndex(uint32_t id) const noexcept {
    return id & (ringBufferCount_ - 1);
  }

  uint16_t ringFillLevel() const noexcept { return ringTail_ - ringHead_; }

  uint16_t ringFreeEntries() const noexcept {
    return ringBufferCount_ - ringFillLevel();
  }

  bool ringIsFull() const noexcept {
    return ringFillLevel() >= ringBufferCount_;
  }

  bool areaIsDrained(const BufferArea& area) const noexcept {
    return area.outstanding.load(std::memory_order_acquire) == 0;
  }

  uint64_t areasOutstandingSum() const noexcept {
    uint64_t outstanding = 0;
    for (auto& area : areas_) {
      outstanding += area->outstanding.load(std::memory_order_relaxed);
    }
    return outstanding;
  }

  struct BufferState {
    uint16_t bufId{0};
    BufferArea* area{nullptr};
    std::atomic<uint32_t> refCount{1};
    unsigned int offset{0};
    IoUringProvidedBufferRing* parent{nullptr};
  };

  struct BufferArea {
    char* buffers{nullptr};
    std::unique_ptr<BufferState[]> states;
    std::atomic<uint32_t> outstanding{0};
    size_t memSize{0};
  };

  std::unique_ptr<BufferArea> createArea() noexcept;

  static void checkInvariants();
  static void bufFreeFn(void*, void* userData) noexcept;

  // Hot fields (cacheline 1)
  alignas(folly::hardware_constructive_interference_size)
      std::vector<std::unique_ptr<BufferArea>> areas_;
  struct io_uring_buf_ring* ringPtr_{nullptr};
  folly::DistributedMutex mutex_;
  uint32_t sizePerBuffer_{0};
  uint32_t ringBufferCount_{0};
  uint16_t ringTail_{0};
  uint16_t ringHead_{0};
  uint16_t areaCount_{0};
  uint16_t const gid_{0};
  uint32_t bufferGetCount_{0};
  uint32_t bufferReturnedCount{0};

  // Hot fields (cacheline 2)
  alignas(folly::hardware_constructive_interference_size) io_uring* ringIoPtr;
  uint32_t shutdownReferences_{0};
  uint32_t enobufCount_{0};
  BufferArea* bufferActiveArea_{nullptr};
  BufferArea* bufferRefillArea_{nullptr};
  bool useIncremental_{false};
  bool enobuf_{false};
  std::atomic<bool> wantsShutdown_{false};
};

} // namespace folly

#endif
