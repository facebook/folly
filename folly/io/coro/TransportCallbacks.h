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

#include <functional>

#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/coro/Task.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/coro/TransportCallbackBase.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

//
// Handle connect for AsyncSocket
//

class ConnectCallback
    : public TransportCallbackBase,
      public folly::AsyncSocket::ConnectCallback {
 public:
  explicit ConnectCallback(folly::AsyncSocket& socket)
      : TransportCallbackBase(socket), socket_(socket) {}

 private:
  void cancel() noexcept override { socket_.cancelConnect(); }

  void connectSuccess() noexcept override { post(); }

  void connectErr(const folly::AsyncSocketException& ex) noexcept override {
    storeException(ex);
    post();
  }
  folly::AsyncSocket& socket_;
};

//
// Handle data read for AsyncTransport
//

class ReadCallback
    : public TransportCallbackBase,
      public folly::AsyncTransport::ReadCallback,
      public folly::HHWheelTimer::Callback {
 public:
  // we need to pass the socket into ReadCallback so we can clear the callback
  // pointer in the socket, thus preventing multiple callbacks from happening
  // in one run of event loop. This may happen, for example, when one fiber
  // writes and immediately closes the socket - this would cause the async
  // socket to call readDataAvailable and readEOF in sequence, causing the
  // promise to be fulfilled twice (oops!)
  ReadCallback(
      folly::HHWheelTimer& timer,
      folly::AsyncTransport& transport,
      folly::MutableByteRange buf,
      std::chrono::milliseconds timeout)
      : TransportCallbackBase(transport), buf_{buf}, timeout_(timeout) {
    scheduleTimeout(timer);
  }

  ReadCallback(
      folly::HHWheelTimer& timer,
      folly::AsyncTransport& transport,
      folly::IOBufQueue* readBuf,
      size_t minReadSize,
      size_t newAllocationSize,
      std::chrono::milliseconds timeout)
      : TransportCallbackBase(transport),
        readBuf_(readBuf),
        minReadSize_(minReadSize),
        newAllocationSize_(newAllocationSize),
        timeout_(timeout) {
    scheduleTimeout(timer);
  }

  void scheduleTimeout(folly::HHWheelTimer& timer) {
    if (timeout_.count() > 0) {
      timer.scheduleTimeout(this, timeout_);
    }
  }

  // how much was read during operation
  size_t length{0};
  bool eof{false};

 private:
  // the read buffer we store to hand off to callback - obtained from user
  folly::MutableByteRange buf_;
  folly::IOBufQueue* readBuf_{nullptr};
  size_t minReadSize_{0};
  size_t newAllocationSize_{0};
  // initial timeout configured on ReadCallback
  std::chrono::milliseconds timeout_;

  void cancel() noexcept override {
    transport_.setReadCB(nullptr);
    cancelTimeout();
  }

  //
  // ReadCallback methods
  //
  bool isBufferMovable() noexcept override { return readBuf_; }

  void readBufferAvailable(
      std::unique_ptr<folly::IOBuf> readBuf) noexcept override {
    CHECK(readBuf_);
    readBuf_->append(std::move(readBuf));
    post();
  }

  // this is called right before readDataAvailable(), always
  // in the same sequence
  void getReadBuffer(void** buf, size_t* len) override {
    if (readBuf_) {
      auto rbuf = readBuf_->preallocate(minReadSize_, newAllocationSize_);
      *buf = rbuf.first;
      *len = rbuf.second;
    } else {
      VLOG(5) << "getReadBuffer, size: " << buf_.size();
      *buf = buf_.begin() + length;
      *len = buf_.size() - length;
    }
  }

  // once we get actual data, uninstall callback and clear timeout
  void readDataAvailable(size_t len) noexcept override {
    VLOG(5) << "readDataAvailable: " << len << " bytes";
    length += len;
    if (readBuf_) {
      readBuf_->postallocate(len);
    } else if (length == buf_.size()) {
      transport_.setReadCB(nullptr);
      cancelTimeout();
    }
    post();
  }

  void readEOF() noexcept override {
    VLOG(5) << "readEOF()";
    // disable callbacks
    transport_.setReadCB(nullptr);
    cancelTimeout();
    eof = true;
    post();
  }

  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    VLOG(5) << "readErr()";
    // disable callbacks
    transport_.setReadCB(nullptr);
    cancelTimeout();
    storeException(ex);
    post();
  }

  //
  // AsyncTimeout method
  //

  void timeoutExpired() noexcept override {
    VLOG(5) << "timeoutExpired()";

    using Error = folly::AsyncSocketException::AsyncSocketExceptionType;

    // uninstall read callback. it takes another read to bring it back.
    transport_.setReadCB(nullptr);
    // If the timeout fires but this ReadCallback did get some data, ignore it.
    // post() has already happend from readDataAvailable.
    if (length == 0) {
      error_ = folly::make_exception_wrapper<folly::AsyncSocketException>(
          Error::TIMED_OUT, "Timed out waiting for data", errno);
      post();
    }
  }
};

//
// Handle data write for AsyncTransport
//

class WriteCallback
    : public TransportCallbackBase,
      public folly::AsyncTransport::WriteCallback {
 public:
  explicit WriteCallback(folly::AsyncTransport& transport)
      : TransportCallbackBase(transport) {}
  ~WriteCallback() override = default;

  size_t bytesWritten{0};
  std::optional<folly::AsyncSocketException> error;

 private:
  void cancel() noexcept override { transport_.closeWithReset(); }
  //
  // Methods of WriteCallback
  //

  void writeSuccess() noexcept override {
    VLOG(5) << "writeSuccess";
    post();
  }

  void writeErr(
      size_t bytes, const folly::AsyncSocketException& ex) noexcept override {
    VLOG(5) << "writeErr, wrote " << bytesWritten << " bytes";
    bytesWritten = bytes;
    error = ex;
    post();
  }
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
