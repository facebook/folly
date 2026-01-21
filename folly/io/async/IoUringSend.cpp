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

#include <folly/io/async/IoUringSend.h>

#include <folly/io/async/EventHandler.h>
#include <folly/io/async/IoUringBackend.h>

namespace folly {

#if FOLLY_HAS_LIBURING

/*
 * SendRequest
 */

class IoUringSendHandle::SendRequest : public IoSqeBase {
 public:
  static void* alloc(size_t iovCount) {
    size_t bufSize = 0;
    if (!checked_muladd<size_t>(
            &bufSize, iovCount, sizeof(struct iovec), sizeof(SendRequest))) {
      throw std::bad_alloc();
    }
    void* buf = malloc(bufSize);
    if (buf == nullptr) {
      throw std::bad_alloc();
    }
    return buf;
  }

  explicit SendRequest(
      AsyncWriter::WriteCallback* callback,
      const struct iovec* iov,
      size_t iovCount,
      size_t partialWritten,
      size_t bytesWritten,
      std::unique_ptr<IOBuf> data,
      WriteFlags flags,
      NetworkSocket fd,
      IoUringSendHandle* handle)
      : IoSqeBase(IoSqeBase::Type::Write),
        callback_(callback),
        releaseIOBufCallback_(
            callback ? callback->getReleaseIOBufCallback() : nullptr),
        iovRemaining_(iovCount),
        bytesWritten_(bytesWritten),
        data_(std::move(data)),
        flags_(flags),
        fd_(fd),
        handle_(handle),
        handleGuard_(handle) {
    if (data_) {
      CHECK_EQ(data_->countChainElements(), iovCount);
    }
    memcpy(iov_, iov, sizeof(struct iovec) * iovCount);
    msg_.msg_iov = iov_;
    msg_.msg_iovlen = std::min<size_t>(iovRemaining_, kIovMax);
    setEventBase(handle_->evb_);

    iov_->iov_base =
        reinterpret_cast<uint8_t*>(iov_->iov_base) + partialWritten;
    iov_->iov_len -= partialWritten;
  }

  void destroy() {
    if (!cancelled() && handle_) {
      handle_->onReleaseIOBuf(std::move(data_), releaseIOBufCallback_);
    }
    this->~SendRequest();
    free(this);
  }

  SendRequest* getNext() { return next_; }
  void append(SendRequest* request) { next_ = request; }
  AsyncWriter::WriteCallback* getCallback() { return callback_; }
  size_t getTotalBytesWritten() { return bytesWritten_; }

  void releaseIOBuf() {
    CHECK(!cancelled());
    handle_->onReleaseIOBuf(std::move(data_), releaseIOBufCallback_);
  }

  /*
   * IoSqeBase
   */
  void processSubmit(struct io_uring_sqe* sqe) noexcept override {
    ::io_uring_prep_sendmsg(sqe, fd_.toFd(), &msg_, flags());
    handle_->onSendStarted();
  }

  void callback(const struct io_uring_cqe* cqe) noexcept override {
    auto res = cqe->res;

    if (cancelled()) {
      return;
    }

    if (res >= 0) {
      consumeBytes(res);
      if (msg_.msg_iovlen > 0) {
        prepareForReuse();
        handle_->onSendPartial(res);
      } else {
        handle_->onSendComplete(res);
      }
    } else {
      handle_->onSendErr(-res);
    }
  }

  void callbackCancelled(const io_uring_cqe*) noexcept override { destroy(); }

 private:
  int flags() {
    int msg_flags = MSG_NOSIGNAL;

    if (isSet(flags_, WriteFlags::CORK)) {
      // MSG_MORE tells the kernel we have more data to send, so wait for us to
      // give it the rest of the data rather than immediately sending a partial
      // frame, even when TCP_NODELAY is enabled.
      msg_flags |= MSG_MORE;
    }

    if (isSet(flags_, WriteFlags::EOR)) {
      // marks that this is the last byte of a record (response)
      msg_flags |= MSG_EOR;
    }

    return msg_flags;
  }

  void consumeBytes(size_t bytes) {
    bytesWritten_ += bytes;

    while (bytes > 0 && iovRemaining_ > 0) {
      if (msg_.msg_iov->iov_len > bytes) {
        msg_.msg_iov->iov_base =
            static_cast<char*>(msg_.msg_iov->iov_base) + bytes;
        msg_.msg_iov->iov_len -= bytes;
        break;
      }

      bytes -= msg_.msg_iov->iov_len;
      ++msg_.msg_iov;
      --iovRemaining_;

      // There is a 1:1 relationship between IOBufs and iovecs.
      if (data_) {
        auto next = data_->pop();
        handle_->onReleaseIOBuf(std::move(data_), releaseIOBufCallback_);
        data_ = std::move(next);
      }
    }

    msg_.msg_iovlen = std::min<size_t>(iovRemaining_, kIovMax);
  }

  AsyncWriter::WriteCallback* callback_;
  AsyncWriter::ReleaseIOBufCallback* releaseIOBufCallback_;
  size_t iovRemaining_;
  size_t bytesWritten_;
  std::unique_ptr<IOBuf> data_;
  WriteFlags flags_;
  NetworkSocket fd_;
  IoUringSendHandle* handle_;
  DestructorGuard handleGuard_;

  SendRequest* next_{nullptr};
  struct msghdr msg_{};

  struct iovec iov_[];
};

/*
 * IoUringSendHandle
 */

IoUringSendHandle::UniquePtr IoUringSendHandle::create(
    EventBase* evb,
    NetworkSocket fd,
    const folly::SocketAddress& addr,
    IoUringSendCallback* callback) {
  auto* backend = dynamic_cast<IoUringBackend*>(evb->getBackend());
  if (!backend) {
    return nullptr;
  }

  return UniquePtr(new IoUringSendHandle(evb, backend, fd, addr, callback));
}

IoUringSendHandle::IoUringSendHandle(
    EventBase* evb,
    IoUringBackend* backend,
    NetworkSocket fd,
    const folly::SocketAddress& addr,
    IoUringSendCallback* callback)
    : evb_(evb),
      backend_(backend),
      fd_(fd),
      addr_(addr),
      sendCallback_(callback) {}

bool IoUringSendHandle::update(uint16_t eventFlags) {
  if (!sendEnabled_ && (eventFlags & EventHandler::WRITE)) {
    sendEnabled_ = true;
    trySubmit();
  } else if (sendEnabled_ && !(eventFlags & EventHandler::WRITE)) {
    sendEnabled_ = false;
  }

  return true;
}

void IoUringSendHandle::write(
    AsyncWriter::WriteCallback* callback,
    const struct iovec* iov,
    size_t iovCount,
    size_t partialWritten,
    size_t bytesWritten,
    std::unique_ptr<IOBuf> data,
    WriteFlags flags) {
  CHECK_GT(iovCount, 0);
  void* buf = SendRequest::alloc(iovCount);
  auto req = new (buf) SendRequest(
      callback,
      iov,
      iovCount,
      partialWritten,
      bytesWritten,
      std::move(data),
      flags,
      fd_,
      this);

  if (requestTail_ == nullptr) {
    CHECK(requestHead_ == nullptr);
    requestHead_ = requestTail_ = req;
    trySubmit();
  } else {
    requestTail_->append(req);
    requestTail_ = req;
  }
}

void IoUringSendHandle::failWrite(const AsyncSocketException& ex) {
  if (requestHead_ != nullptr) {
    auto req = requestHead_;
    requestHead_ = req->getNext();
    auto* callback = req->getCallback();
    auto bytesWritten = req->getTotalBytesWritten();

    if (req->inFlight()) {
      // AsyncSocket may be gone by the time the cancelled callback runs, so
      // release the IOBuf now.
      req->releaseIOBuf();
      backend_->cancel(req);
    } else {
      req->destroy();
    }

    if (callback) {
      callback->writeErr(bytesWritten, ex);
    }
  }
}

void IoUringSendHandle::failAllWrites(const AsyncSocketException& ex) {
  while (requestHead_ != nullptr) {
    failWrite(ex);
  }
}

void IoUringSendHandle::trySubmit() {
  if (sendEnabled_ && requestHead_ && !requestHead_->inFlight()) {
    backend_->submitSoon(*requestHead_);
  }
}

void IoUringSendHandle::onSendStarted() {
  requestHead_->getCallback()->writeStarting();
}

void IoUringSendHandle::onSendPartial(size_t bytesWritten) {
  CHECK(requestHead_ != nullptr);
  sendCallback_->sendPartial(bytesWritten);
  trySubmit();
}

void IoUringSendHandle::onSendComplete(size_t bytesWritten) {
  DestructorGuard dg(this);
  CHECK(requestHead_ != nullptr);
  auto req = requestHead_;
  requestHead_ = req->getNext();
  if (requestHead_ == nullptr) {
    requestTail_ = nullptr;
    sendCallback_->sendDone(bytesWritten);
    // sets sendEnabled_ to false
  }

  auto* callback = req->getCallback();
  req->destroy();
  if (callback) {
    callback->writeSuccess();
  }

  trySubmit();
}

void IoUringSendHandle::onSendErr(int err) {
  sendCallback_->sendErr(err);
}

void IoUringSendHandle::onReleaseIOBuf(
    std::unique_ptr<IOBuf> data, AsyncWriter::ReleaseIOBufCallback* callback) {
  sendCallback_->releaseIOBuf(std::move(data), callback);
}

#else

IoUringSendHandle::UniquePtr IoUringSendHandle::create(
    EventBase* /*evb*/,
    NetworkSocket /*fd*/,
    const folly::SocketAddress& /*addr*/,
    IoUringSendCallback* /*callback*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

IoUringSendHandle::IoUringSendHandle(
    EventBase* /*evb*/,
    IoUringBackend* /*backend*/,
    NetworkSocket /*fd*/,
    const folly::SocketAddress& /*addr*/,
    IoUringSendCallback* /*callback*/) {
  (void)evb_;
  (void)backend_;
  (void)fd_;
  (void)addr_;
  (void)sendCallback_;
  (void)requestHead_;
  (void)requestTail_;
  (void)sendEnabled_;
}

bool IoUringSendHandle::update(uint16_t /*eventFlags*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::write(
    AsyncWriter::WriteCallback* /*callback*/,
    const struct iovec* /*iov*/,
    size_t /*iovCount*/,
    size_t /*partialWritten*/,
    size_t /*bytesWritten*/,
    std::unique_ptr<IOBuf> /*data*/,
    WriteFlags /*flags*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::failWrite(const AsyncSocketException& /*ex*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::failAllWrites(const AsyncSocketException& /*ex*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::trySubmit() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::onSendStarted() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::onSendPartial(size_t /*bytesWritten*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::onSendComplete(size_t /*bytesWritten*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::onSendErr(int /*err*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringSendHandle::onReleaseIOBuf(
    std::unique_ptr<IOBuf> /*data*/,
    AsyncWriter::ReleaseIOBufCallback* /*callback*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

#endif

} // namespace folly
