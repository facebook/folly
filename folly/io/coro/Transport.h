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

#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/experimental/coro/Task.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

class TransportIf {
 public:
  using ErrorCode = AsyncSocketException::AsyncSocketExceptionType;
  // on write error, report the issue and how many bytes were written
  virtual ~TransportIf() = default;
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
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) = 0;
  virtual Task<Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) = 0;

  virtual SocketAddress getLocalAddress() const noexcept = 0;

  virtual SocketAddress getPeerAddress() const noexcept = 0;
  virtual void close() = 0;
  virtual void shutdownWrite() = 0;
  virtual void closeWithReset() = 0;
  virtual folly::AsyncTransport* getTransport() const = 0;
  virtual const AsyncTransportCertificate* getPeerCertificate() const = 0;
};

class Transport : public TransportIf {
 public:
  Transport(
      folly::EventBase* eventBase, folly::AsyncTransport::UniquePtr transport)
      : eventBase_(eventBase), transport_(std::move(transport)) {}

  Transport(Transport&&) = default;
  Transport& operator=(Transport&&) = default;

  // Establish a TCP connection to the given address and return a Transport
  // That wraps that socket
  static Task<Transport> newConnectedSocket(
      EventBase* evb,
      const SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout,
      const SocketOptionMap& options = emptySocketOptionMap,
      const SocketAddress& bindAddr = AsyncSocketTransport::anyAddress(),
      const std::string& ifName = "");
  virtual EventBase* getEventBase() noexcept override { return eventBase_; }

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
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) override;
  Task<folly::Unit> write(
      IOBufQueue& ioBufQueue,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      folly::WriteFlags writeFlags = folly::WriteFlags::NONE,
      WriteInfo* writeInfo = nullptr) override;

  AsyncTransport* getTransport() const override { return transport_.get(); }

  SocketAddress getLocalAddress() const noexcept override {
    SocketAddress addr;
    transport_->getLocalAddress(&addr);
    return addr;
  }

  SocketAddress getPeerAddress() const noexcept override {
    SocketAddress addr;
    transport_->getPeerAddress(&addr);
    return addr;
  }

  void shutdownWrite() noexcept override {
    if (transport_) {
      transport_->shutdownWrite();
    }
  }

  void close() noexcept override {
    if (transport_) {
      transport_->close();
    }
  }

  void closeWithReset() noexcept override {
    if (transport_) {
      transport_->closeWithReset();
    }
  }

  const AsyncTransportCertificate* getPeerCertificate() const override {
    return transport_->getPeerCertificate();
  }

 private:
  // non-copyable
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  EventBase* eventBase_;
  AsyncTransport::UniquePtr transport_;
  bool deferredReadEOF_{false};
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
