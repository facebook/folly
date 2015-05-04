/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/wangle/channel/Handler.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

namespace folly { namespace wangle {

// This handler may only be used in a single Pipeline
class AsyncSocketHandler
  : public folly::wangle::BytesToBytesHandler,
    public AsyncSocket::ReadCallback {
 public:
  explicit AsyncSocketHandler(
      std::shared_ptr<AsyncSocket> socket)
    : socket_(std::move(socket)) {}

  AsyncSocketHandler(AsyncSocketHandler&&) = default;

  ~AsyncSocketHandler() {
    if (socket_) {
      detachReadCallback();
    }
  }

  void attachReadCallback() {
    socket_->setReadCB(socket_->good() ? this : nullptr);
  }

  void detachReadCallback() {
    if (socket_->getReadCallback() == this) {
      socket_->setReadCB(nullptr);
    }
  }

  void attachEventBase(folly::EventBase* eventBase) {
    if (eventBase && !socket_->getEventBase()) {
      socket_->attachEventBase(eventBase);
    }
  }

  void detachEventBase() {
    detachReadCallback();
    if (socket_->getEventBase()) {
      socket_->detachEventBase();
    }
  }

  void attachPipeline(Context* ctx) override {
    attachReadCallback();
  }

  folly::Future<void> write(
      Context* ctx,
      std::unique_ptr<folly::IOBuf> buf) override {
    if (UNLIKELY(!buf)) {
      return folly::makeFuture();
    }

    if (!socket_->good()) {
      VLOG(5) << "socket is closed in write()";
      return folly::makeFuture<void>(AsyncSocketException(
          AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
          "socket is closed in write()"));
    }

    auto cb = new WriteCallback();
    auto future = cb->promise_.getFuture();
    socket_->writeChain(cb, std::move(buf), ctx->getWriteFlags());
    return future;
  };

  folly::Future<void> close(Context* ctx) override {
    if (socket_) {
      detachReadCallback();
      socket_->closeNow();
    }
    ctx->getPipeline()->deletePipeline();
    return folly::makeFuture();
  }

  // Must override to avoid warnings about hidden overloaded virtual due to
  // AsyncSocket::ReadCallback::readEOF()
  void readEOF(Context* ctx) override {
    ctx->fireReadEOF();
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    const auto readBufferSettings = getContext()->getReadBufferSettings();
    const auto ret = bufQueue_.preallocate(
        readBufferSettings.first,
        readBufferSettings.second);
    *bufReturn = ret.first;
    *lenReturn = ret.second;
  }

  void readDataAvailable(size_t len) noexcept override {
    bufQueue_.postallocate(len);
    getContext()->fireRead(bufQueue_);
  }

  void readEOF() noexcept override {
    getContext()->fireReadEOF();
  }

  void readErr(const AsyncSocketException& ex)
    noexcept override {
    getContext()->fireReadException(
        make_exception_wrapper<AsyncSocketException>(ex));
  }

 private:
  class WriteCallback : private AsyncSocket::WriteCallback {
    void writeSuccess() noexcept override {
      promise_.setValue();
      delete this;
    }

    void writeErr(size_t bytesWritten,
                    const AsyncSocketException& ex)
      noexcept override {
      promise_.setException(ex);
      delete this;
    }

   private:
    friend class AsyncSocketHandler;
    folly::Promise<void> promise_;
  };

  folly::IOBufQueue bufQueue_{folly::IOBufQueue::cacheChainLength()};
  std::shared_ptr<AsyncSocket> socket_{nullptr};
};

}}
