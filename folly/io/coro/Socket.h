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

#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Utils.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>

namespace folly {
namespace coro {

class Transport {
 public:
  using ErrorCode = AsyncSocketException::AsyncSocketExceptionType;
  // on write error, report the issue and how many bytes were written
  virtual ~Transport() = default;
  virtual EventBase* getEventBase() noexcept = 0;
  virtual Task<size_t> read(
      MutableByteRange buf, std::chrono::milliseconds timeout) = 0;
  Task<size_t> read(
      void* buf, size_t buflen, std::chrono::milliseconds timeout) {
    return read(MutableByteRange((unsigned char*)buf, buflen), timeout);
  }
  virtual Task<size_t> read(
      IOBufQueue& buf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout) = 0;

  struct WriteInfo {
    size_t bytesWritten{0};
  };

  virtual Task<Unit> write(
      ByteRange buf,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      WriteInfo* writeInfo = nullptr) = 0;
  virtual Task<Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      WriteInfo* writeInfo = nullptr) = 0;

  virtual SocketAddress getLocalAddress() const noexcept = 0;

  virtual SocketAddress getPeerAddress() const noexcept = 0;
  virtual void close() = 0;
  virtual void shutdownWrite() = 0;
  virtual void closeWithReset() = 0;
  virtual folly::AsyncTransport* getTransport() const = 0;
  virtual const AsyncTransportCertificate* getPeerCertificate() const = 0;
};

class Socket : public Transport {
 public:
  explicit Socket(std::shared_ptr<AsyncSocket> socket)
      : socket_(std::move(socket)) {}

  Socket(Socket&&) = default;
  Socket& operator=(Socket&&) = default;

  static Task<Socket> connect(
      EventBase* evb,
      const SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout);
  virtual EventBase* getEventBase() noexcept override {
    return socket_->getEventBase();
  }

  Task<size_t> read(
      MutableByteRange buf, std::chrono::milliseconds timeout) override;
  Task<size_t> read(
      IOBufQueue& buf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout) override;

  Task<Unit> write(
      ByteRange buf,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      WriteInfo* writeInfo = nullptr) override;
  Task<folly::Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      WriteInfo* writeInfo = nullptr) override;

  SocketAddress getLocalAddress() const noexcept override {
    SocketAddress addr;
    socket_->getLocalAddress(&addr);
    return addr;
  }

  folly::AsyncTransport* getTransport() const override { return socket_.get(); }

  SocketAddress getPeerAddress() const noexcept override {
    SocketAddress addr;
    socket_->getPeerAddress(&addr);
    return addr;
  }

  void shutdownWrite() noexcept override {
    if (socket_) {
      socket_->shutdownWrite();
    }
  }

  void close() noexcept override {
    if (socket_) {
      socket_->close();
    }
  }

  void closeWithReset() noexcept override {
    if (socket_) {
      socket_->closeWithReset();
    }
  }

  std::shared_ptr<AsyncSocket> getAsyncSocket() { return socket_; }

  const AsyncTransportCertificate* getPeerCertificate() const override {
    return socket_->getPeerCertificate();
  }

 private:
  // non-copyable
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  std::shared_ptr<AsyncSocket> socket_;
  bool deferredReadEOF_{false};
};

} // namespace coro
} // namespace folly
