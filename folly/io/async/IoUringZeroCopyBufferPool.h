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

#include <folly/io/IOBuf.h>
#include <folly/io/async/Liburing.h>
#include <folly/synchronization/DistributedMutex.h>

#if FOLLY_HAS_LIBURING

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wnested-anon-types")
FOLLY_CLANG_DISABLE_WARNING("-Wzero-length-array")
#include <liburing.h> // @manual
FOLLY_POP_WARNING

namespace folly {

class IoUringZeroCopyBufferPool {
 public:
  struct Deleter {
    void operator()(IoUringZeroCopyBufferPool* base);
  };

  struct Params {
    io_uring* ring;
    size_t numPages;
    size_t pageSize;
    uint32_t rqEntries;
    uint32_t ifindex;
    uint16_t queueId;
  };

  // Only support heap construction with a custom Deleter. This is to avoid
  // deleting the object until all buffers have been returned.
  using UniquePtr = std::unique_ptr<IoUringZeroCopyBufferPool, Deleter>;
  static IoUringZeroCopyBufferPool::UniquePtr create(Params params);

  ~IoUringZeroCopyBufferPool() = default;

  void destroy() noexcept;
  std::unique_ptr<IOBuf> getIoBuf(
      const io_uring_cqe* cqe, const io_uring_zcrx_cqe* rcqe) noexcept;

 private:
  explicit IoUringZeroCopyBufferPool(Params params);

  IoUringZeroCopyBufferPool(IoUringZeroCopyBufferPool&&) = delete;
  IoUringZeroCopyBufferPool(IoUringZeroCopyBufferPool const&) = delete;
  IoUringZeroCopyBufferPool& operator=(IoUringZeroCopyBufferPool&&) = delete;
  IoUringZeroCopyBufferPool& operator=(IoUringZeroCopyBufferPool const&) =
      delete;

  struct Buffer {
    uint64_t off;
    uint32_t len;
    IoUringZeroCopyBufferPool* pool;
  };

  void mapMemory();
  void initialRegister(uint32_t ifindex, uint16_t queueId);

  void returnBuffer(Buffer* buf) noexcept;

  void delayedDestroy(uint32_t refs) noexcept;

  io_uring* ring_{nullptr};
  size_t pageSize_{0};
  uint32_t rqEntries_{0};

  void* bufArea_{nullptr};
  size_t bufAreaSize_{0};
  std::vector<Buffer> buffers_;
  void* rqRingArea_{nullptr};
  size_t rqRingAreaSize_{0};
  // Ring buffer shared between kernel and userspace
  // Constructed in initialRegister()
  io_uring_zcrx_rq rqRing_;
  uint64_t rqAreaToken_{0};
  uint64_t rqTail_{0};
  unsigned rqMask_{0};
  uint64_t bufDispensed_{0};

  folly::DistributedMutex mutex_;
  std::atomic<bool> wantsShutdown_{false};
  uint32_t shutdownReferences_{0};
};

} // namespace folly

#endif
