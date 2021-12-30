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

#include <folly/SharedMutex.h>
#include <folly/experimental/io/AsyncBase.h>

#if __has_include(<liburing.h>)

#include <liburing.h>

namespace folly {

/**
 * An IoUringOp represents a pending operation.  You may set a notification
 * callback or you may use this class's methods directly.
 *
 * The op must remain allocated until it is completed or canceled.
 */
class IoUringOp : public AsyncBaseOp {
  friend class IoUring;
  friend std::ostream& operator<<(std::ostream& stream, const IoUringOp& o);

 public:
  explicit IoUringOp(NotificationCallback cb = NotificationCallback());
  IoUringOp(const IoUringOp&) = delete;
  IoUringOp& operator=(const IoUringOp&) = delete;
  ~IoUringOp() override;

  /**
   * Initiate a read request.
   */
  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pread(
      int fd, void* buf, size_t size, off_t start, int buf_index) override;

  /**
   * Initiate a write request.
   */
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pwrite(int fd, const void* buf, size_t size, off_t start, int buf_index)
      override;

  void reset(NotificationCallback cb = NotificationCallback()) override;

  AsyncIOOp* getAsyncIOOp() override { return nullptr; }

  IoUringOp* getIoUringOp() override { return this; }

  void toStream(std::ostream& os) const override;

  const struct io_uring_sqe& getSqe() const { return sqe_; }

 private:
  struct io_uring_sqe sqe_;
  struct iovec iov_[1];
};

std::ostream& operator<<(std::ostream& stream, const IoUringOp& op);

/**
 * C++ interface around Linux io_uring
 */
class IoUring : public AsyncBase {
 public:
  using Op = IoUringOp;

  /**
   * Note: the maximum number of allowed concurrent requests is controlled
   * by the kernel IORING_MAX_ENTRIES and the memlock limit,
   * The default IORING_MAX_ENTRIES value is usually 32K.
   */
  explicit IoUring(
      size_t capacity, PollMode pollMode = NOT_POLLABLE, size_t maxSubmit = 1);
  IoUring(const IoUring&) = delete;
  IoUring& operator=(const IoUring&) = delete;
  ~IoUring() override;

  static bool isAvailable();

  int register_buffers(const struct iovec* iovecs, unsigned int nr_iovecs);

  int unregister_buffers();

  void initializeContext() override;

 protected:
  int submitOne(AsyncBase::Op* op) override;
  int submitRange(Range<AsyncBase::Op**> ops) override;

 private:
  Range<AsyncBase::Op**> doWait(
      WaitType type,
      size_t minRequests,
      size_t maxRequests,
      std::vector<AsyncBase::Op*>& result) override;

  size_t maxSubmit_;
  struct io_uring_params params_;
  struct io_uring ioRing_;
  SharedMutex submitMutex_;
};

using IoUringQueue = AsyncBaseQueue;
} // namespace folly

#endif
