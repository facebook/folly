/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <libaio.h>

#include <folly/experimental/io/AsyncBase.h>

namespace folly {

class AsyncIOOp : public AsyncBaseOp {
  friend class AsyncIO;
  friend std::ostream& operator<<(std::ostream& os, const AsyncIOOp& o);

 public:
  explicit AsyncIOOp(NotificationCallback cb = NotificationCallback());
  AsyncIOOp(const AsyncIOOp&) = delete;
  AsyncIOOp& operator=(const AsyncIOOp&) = delete;
  ~AsyncIOOp() override;

  /**
   * Initiate a read request.
   */
  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;

  /**
   * Initiate a write request.
   */
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;

  void reset(NotificationCallback cb = NotificationCallback()) override;

  AsyncIOOp* getAsyncIOOp() override { return this; }

  IoUringOp* getIoUringOp() override { return nullptr; }

  void toStream(std::ostream& os) const override;

  const iocb& getIocb() const { return iocb_; }

 private:
  iocb iocb_;
};

std::ostream& operator<<(std::ostream& os, const AsyncIOOp& op);

/**
 * C++ interface around Linux Async IO.
 */
class AsyncIO : public AsyncBase {
 public:
  using Op = AsyncIOOp;

  /**
   * Note: the maximum number of allowed concurrent requests is controlled
   * by the fs.aio-max-nr sysctl, the default value is usually 64K.
   */
  explicit AsyncIO(size_t capacity, PollMode pollMode = NOT_POLLABLE);
  AsyncIO(const AsyncIO&) = delete;
  AsyncIO& operator=(const AsyncIO&) = delete;
  ~AsyncIO() override;

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

  io_context_t ctx_{nullptr};
};

using AsyncIOQueue = AsyncBaseQueue;
} // namespace folly
