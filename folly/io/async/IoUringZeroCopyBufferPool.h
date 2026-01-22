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

#include <memory>
#include <folly/io/IOBuf.h>
#include <folly/io/async/Liburing.h>

#if FOLLY_HAS_LIBURING
FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wnested-anon-types")
FOLLY_CLANG_DISABLE_WARNING("-Wzero-length-array")
FOLLY_GCC_DISABLE_WARNING("-Wshadow")
#include <liburing.h> // @manual
FOLLY_POP_WARNING

namespace folly {

class IoUringZeroCopyBufferPoolImpl;

class IoUringZeroCopyBufferPool {
 public:
  struct Params {
    struct io_uring* ring;
    size_t numPages;
    size_t pageSize;
    uint32_t rqEntries;
    uint32_t ifindex;
    uint16_t queueId;
  };

  struct ExportHandle {
    explicit ExportHandle(
        int zcrxFd, std::shared_ptr<IoUringZeroCopyBufferPoolImpl> impl)
        : zcrxFd_(zcrxFd), impl_(std::move(impl)) {}

    ~ExportHandle() = default;

    ExportHandle(ExportHandle&&) = default;
    ExportHandle& operator=(ExportHandle&&) = default;
    ExportHandle(const ExportHandle&) = delete;
    ExportHandle& operator=(const ExportHandle&) = delete;

   private:
    friend class IoUringZeroCopyBufferPool;

    int zcrxFd_;
    std::shared_ptr<IoUringZeroCopyBufferPoolImpl> impl_;
  };

  using UniquePtr = std::unique_ptr<IoUringZeroCopyBufferPool>;
  static UniquePtr create(Params params);
  static UniquePtr importHandle(ExportHandle handle, struct io_uring* ring);

  ExportHandle exportHandle() const;

  ~IoUringZeroCopyBufferPool();

  std::unique_ptr<IOBuf> getIoBuf(
      const struct io_uring_cqe* cqe,
      const struct io_uring_zcrx_cqe* rcqe) noexcept;

 private:
  explicit IoUringZeroCopyBufferPool(Params params);

  struct TestTag {};
  explicit IoUringZeroCopyBufferPool(Params params, TestTag);

  explicit IoUringZeroCopyBufferPool(
      ExportHandle handle, struct io_uring* ring);

  IoUringZeroCopyBufferPool(IoUringZeroCopyBufferPool&&) = delete;
  IoUringZeroCopyBufferPool(IoUringZeroCopyBufferPool const&) = delete;
  IoUringZeroCopyBufferPool& operator=(IoUringZeroCopyBufferPool&&) = delete;
  IoUringZeroCopyBufferPool& operator=(IoUringZeroCopyBufferPool const&) =
      delete;

  // For testing
  friend class IoUringZeroCopyBufferPoolTestHelper;
  uint32_t* getHead() const noexcept;
  uint32_t getRingUsedCount() const noexcept;
  uint32_t getRingFreeCount() const noexcept;
  size_t getPendingBuffersSize() const noexcept;
  uint32_t getFlushThreshold() const noexcept;
  uint16_t getAndResetFlushFailures() noexcept;
  uint16_t getAndResetFlushCount() noexcept;

  struct io_uring* ring_{nullptr};
  std::shared_ptr<IoUringZeroCopyBufferPoolImpl> impl_;
  int zcrxId_{-1};
  int zcrxFd_{-1};
};

} // namespace folly

#endif
