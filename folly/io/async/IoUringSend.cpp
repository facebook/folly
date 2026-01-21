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
      NetworkSocket fd)
      : IoSqeBase(IoSqeBase::Type::Write),
        callback_(callback),
        iovRemaining_(iovCount),
        bytesWritten_(bytesWritten),
        data_(std::move(data)),
        flags_(flags),
        fd_(fd) {
    if (data_) {
      CHECK_EQ(data_->countChainElements(), iovCount);
    }
    memcpy(iov_, iov, sizeof(struct iovec) * iovCount);
    msg_.msg_iov = iov_;
    msg_.msg_iovlen = std::min<size_t>(iovRemaining_, kIovMax);

    iov_->iov_base =
        reinterpret_cast<uint8_t*>(iov_->iov_base) + partialWritten;
    iov_->iov_len -= partialWritten;
  }

  void destroy() {
    if (!cancelled() && handle_) {
      handle_->onReleaseIOBuf(
          std::move(data_), callback_->getReleaseIOBufCallback());
    }
    this->~SendRequest();
    free(this);
  }

  void setHandle(IoUringSendHandle* handle) {
    handle_ = handle;
    handleGuard_ = DestructorGuard(handle);
  }
  SendRequest* getNext() { return next_; }
  void append(SendRequest* request) { next_ = request; }
  AsyncWriter::WriteCallback* getCallback() { return callback_; }
  size_t getTotalBytesWritten() { return bytesWritten_; }

  void releaseIOBuf(IoUringSendHandle* handle) {
    CHECK(!cancelled());
    handle->onReleaseIOBuf(
        std::move(data_), callback_->getReleaseIOBufCallback());
  }

  folly::SemiFuture<int> detachEventBase() {
    handle_ = nullptr;
    setEventBase(nullptr);

    auto [promise, future] = makePromiseContract<int>();
    detachedSignal_ = [p = std::move(promise)](int res) mutable {
      p.setValue(res);
    };
    return std::move(future);
  }

  SendRequest* clone(IoUringSendHandle* newHandle) {
    CHECK(handle_ == nullptr);
    void* buf = alloc(msg_.msg_iovlen);
    auto clone = new (buf) SendRequest(
        callback_,
        msg_.msg_iov,
        msg_.msg_iovlen,
        0,
        bytesWritten_,
        std::move(data_),
        flags_,
        fd_);
    clone->append(next_);
    if (inFlight()) {
      clone->internalMarkInflight(true);
      clone->setHandle(newHandle);
    }
    return clone;
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

    if (!handle_) {
      detachedSignal_(res);
      return;
    }

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
        handle_->onReleaseIOBuf(
            std::move(data_), callback_->getReleaseIOBufCallback());
        data_ = std::move(next);
      }
    }

    msg_.msg_iovlen = std::min<size_t>(iovRemaining_, kIovMax);
  }

  AsyncWriter::WriteCallback* callback_;
  size_t iovRemaining_;
  size_t bytesWritten_;
  std::unique_ptr<IOBuf> data_;
  WriteFlags flags_;
  NetworkSocket fd_;

  IoUringSendHandle* handle_{nullptr};
  DestructorGuard handleGuard_{nullptr};
  SendRequest* next_{nullptr};
  struct msghdr msg_{};
  folly::Function<void(int)> detachedSignal_;

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

IoUringSendHandle::UniquePtr IoUringSendHandle::clone(
    EventBase* evb, IoUringSendHandle::UniquePtr other) {
  auto* backend = dynamic_cast<IoUringBackend*>(evb->getBackend());
  if (!backend) {
    return nullptr;
  }

  return UniquePtr(new IoUringSendHandle(evb, backend, std::move(other)));
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

IoUringSendHandle::IoUringSendHandle(
    EventBase* evb, IoUringBackend* backend, IoUringSendHandle::UniquePtr other)
    : evb_(evb),
      backend_(backend),
      fd_(other->fd_),
      addr_(other->addr_),
      sendCallback_(other->sendCallback_) {
  if (!other->empty()) {
    auto* oldReq = other->requestHead_;
    auto* newReq = oldReq->clone(this);
    requestHead_ = newReq;
    requestTail_ = newReq->getNext() == nullptr ? newReq : other->requestTail_;

    if (other->detachedFuture_.has_value()) {
      CHECK(oldReq->inFlight());
      CHECK(newReq->inFlight());
      std::move(*other->detachedFuture_)
          .via(evb)
          .thenValue([oldReq, newReq, evb](int res) {
            // The result res is from detachSignal_ in the previous request
            oldReq->destroy();
            struct io_uring_cqe cqe{};
            cqe.res = res;
            evb->bumpHandlingTime();
            if (newReq->cancelled()) {
              newReq->callbackCancelled(&cqe);
            } else {
              newReq->callback(&cqe);
            }
          });
    }
  }
}

void IoUringSendHandle::detachEventBase() {
  CHECK(!sendEnabled_);
  evb_ = nullptr;
  backend_ = nullptr;

  if (requestHead_ && requestHead_->inFlight()) {
    detachedFuture_ = requestHead_->detachEventBase();
  }
}

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
      fd_);
  req->setEventBase(evb_);

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

    req->releaseIOBuf(this);
    if (req->inFlight()) {
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
    requestHead_->setHandle(this);
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
  (void)detachedFuture_;
}

IoUringSendHandle::UniquePtr IoUringSendHandle::clone(
    EventBase* /*evb*/, IoUringSendHandle::UniquePtr /*other*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

IoUringSendHandle::IoUringSendHandle(
    EventBase* /*evb*/,
    IoUringBackend* /*backend*/,
    IoUringSendHandle::UniquePtr /*other*/) {
  (void)evb_;
  (void)backend_;
  (void)fd_;
  (void)addr_;
  (void)sendCallback_;
  (void)requestHead_;
  (void)requestTail_;
  (void)sendEnabled_;
  (void)detachedFuture_;
}

void IoUringSendHandle::detachEventBase() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
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
