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

#include <folly/Optional.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/SSLContext.h>
#include <folly/net/NetworkSocket.h>

class BlockingSocket : public folly::AsyncSocket::ConnectCallback,
                       public folly::AsyncTransport::ReadCallback,
                       public folly::AsyncTransport::WriteCallback {
 public:
  explicit BlockingSocket(folly::NetworkSocket fd)
      : sock_(new folly::AsyncSocket(&eventBase_, fd)) {}

  BlockingSocket(
      folly::SocketAddress address,
      std::shared_ptr<folly::SSLContext> sslContext)
      : sock_(
            sslContext ? new folly::AsyncSSLSocket(sslContext, &eventBase_)
                       : new folly::AsyncSocket(&eventBase_)),
        address_(address) {}

  explicit BlockingSocket(folly::AsyncSocket::UniquePtr socket)
      : sock_(std::move(socket)) {
    sock_->attachEventBase(&eventBase_);
  }

  void enableTFO() { sock_->enableTFO(); }

  void setEorTracking(bool track) { sock_->setEorTracking(track); }

  void setAddress(folly::SocketAddress address) { address_ = address; }

  void open(
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
    DCHECK_LE(timeout.count(), std::numeric_limits<int>::max());
    sock_->connect(this, address_, folly::to_narrow(timeout.count()));
    eventBase_.loop();
    if (err_.has_value()) {
      throw err_.value();
    }
  }

  void close() { sock_->close(); }
  void closeWithReset() { sock_->closeWithReset(); }

  int32_t write(
      uint8_t const* buf,
      size_t len,
      folly::WriteFlags flags = folly::WriteFlags::NONE) {
    sock_->write(this, buf, len, flags);
    eventBase_.loop();
    if (err_.has_value()) {
      throw err_.value();
    }
    return folly::to_narrow(folly::to_signed(len));
  }

  void writev(
      const iovec* vec,
      size_t count,
      folly::WriteFlags flags = folly::WriteFlags::NONE) {
    sock_->writev(this, vec, count, flags);
    eventBase_.loop();
    if (err_.has_value()) {
      throw err_.value();
    }
  }

  void flush() {}

  int32_t readAll(uint8_t* buf, size_t len) {
    return readHelper(buf, len, true);
  }

  int32_t read(uint8_t* buf, size_t len) { return readHelper(buf, len, false); }

  int32_t readNoBlock(uint8_t* buf, size_t len) {
    return readHelper(buf, len, false, EVLOOP_NONBLOCK);
  }

  folly::NetworkSocket getNetworkSocket() const {
    return sock_->getNetworkSocket();
  }

  folly::AsyncSocket* getSocket() { return sock_.get(); }

  folly::AsyncSSLSocket* getSSLSocket() {
    return dynamic_cast<folly::AsyncSSLSocket*>(sock_.get());
  }

 private:
  folly::EventBase eventBase_;
  folly::AsyncSocket::UniquePtr sock_;
  folly::Optional<folly::AsyncSocketException> err_;
  uint8_t* readBuf_{nullptr};
  size_t readLen_{0};
  folly::SocketAddress address_;

  void connectSuccess() noexcept override {}
  void connectErr(const folly::AsyncSocketException& ex) noexcept override {
    err_ = ex;
  }
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = readBuf_;
    *lenReturn = readLen_;
  }
  void readDataAvailable(size_t len) noexcept override {
    readBuf_ += len;
    readLen_ -= len;
    if (readLen_ == 0) {
      sock_->setReadCB(nullptr);
    }
  }
  void readEOF() noexcept override {}
  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    err_ = ex;
  }
  void writeSuccess() noexcept override {}
  void writeErr(
      size_t /* bytesWritten */,
      const folly::AsyncSocketException& ex) noexcept override {
    err_ = ex;
  }

  int32_t readHelper(uint8_t* buf, size_t len, bool all, int flags = 0) {
    if (!sock_->good()) {
      return 0;
    }
    readBuf_ = buf;
    readLen_ = len;
    sock_->setReadCB(this);
    while (!err_ && sock_->good() && readLen_ > 0) {
      eventBase_.loopOnce(flags);
      if (!all) {
        break;
      }
    }
    sock_->setReadCB(nullptr);
    if (err_.has_value()) {
      throw err_.value();
    }
    if (all && readLen_ > 0) {
      throw folly::AsyncSocketException(
          folly::AsyncSocketException::UNKNOWN, "eof");
    }
    return folly::to_narrow(folly::to_signed(len - readLen_));
  }
};
