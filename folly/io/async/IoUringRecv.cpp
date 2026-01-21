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

#include <folly/io/async/IoUringRecv.h>

#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/IoUringBackend.h>

namespace folly {

#if FOLLY_HAS_LIBURING

/*
 * RecvRequest
 */

class IoUringRecvHandle::RecvRequest
    : public IoSqeBase,
      public DelayedDestruction {
 public:
  using UniquePtr = std::unique_ptr<RecvRequest, Destructor>;

  explicit RecvRequest(
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvHandle* handle)
      : IoSqeBase(IoSqeBase::Type::Read),
        fd_(fd),
        handle_(handle),
        handleGuard_(handle) {
    if (backend->zcBufferPool() && !addr.isLoopbackAddress()) {
      bufferPool_ = backend->zcBufferPool();
    } else {
      bufferRing_ = backend->bufferProvider();
    }
  }

  void setRecvLen(size_t len) { recvLen_ = len; }

  void prepRecvNormal(struct io_uring_sqe* sqe) {
    if (recvLen_ > 0) {
      ::io_uring_prep_recv(sqe, fd_.toFd(), nullptr, recvLen_, 0);
    } else {
      ::io_uring_prep_recv_multishot(sqe, fd_.toFd(), nullptr, 0, 0);
    }
    sqe->buf_group = bufferRing_->gid();
    sqe->flags |= IOSQE_BUFFER_SELECT;
  }

  void prepRecvFallback(struct io_uring_sqe* sqe) {
    size_t size = recvLen_ > 0 ? recvLen_ : goodMallocSize(16384);
    fallbackBuffer_ = IOBuf::create(size);
    ::io_uring_prep_recv(
        sqe,
        fd_.toFd(),
        fallbackBuffer_->writableTail(),
        fallbackBuffer_->tailroom(),
        0);
  }

  /*
   * IoSqeBase
   */
  void processSubmit(struct io_uring_sqe* sqe) noexcept override {
    fallbackBuffer_.reset();

    if (bufferPool_) {
      ::io_uring_prep_rw(
          IORING_OP_RECV_ZC,
          sqe,
          fd_.toFd(),
          nullptr,
          static_cast<uint32_t>(recvLen_),
          0);
      sqe->ioprio |= IORING_RECV_MULTISHOT;
      return;
    }

    if (bufferRing_->available()) {
      prepRecvNormal(sqe);
    } else {
      prepRecvFallback(sqe);
    }
  }

  void callback(const struct io_uring_cqe* cqe) noexcept override {
    DestructorGuard dg(this);
    auto res = cqe->res;

    if (res > 0) {
      handle_->onRecvComplete(getData(cqe));
      return;
    }

    if (res == -ECANCELED) {
      // ignore
      return;
    } else if (res == -ENOBUFS) {
      bufferRing_->enobuf();
      handle_->onEnobufs();
      return;
    } else if (isEOF(cqe)) {
      handle_->onRecvEOF();
    } else {
      handle_->onRecvErr(-res);
    }
  }

  void callbackCancelled(const io_uring_cqe* cqe) noexcept override {
    DestructorGuard dg(this);

    if (cqe->res > 0) {
      handle_->onRecvComplete(getData(cqe));
    }

    if (!(cqe->flags & IORING_CQE_F_MORE)) {
      handle_->onRecvEOF();
      destroy();
    }
  }

 private:
  std::unique_ptr<IOBuf> getData(const struct io_uring_cqe* cqe) {
    if (bufferPool_) {
      const auto* rcqe = (struct io_uring_zcrx_cqe*)(cqe + 1);
      return bufferPool_->getIoBuf(cqe, rcqe);
    }

    if (fallbackBuffer_) {
      fallbackBuffer_->append(cqe->res);
      return std::move(fallbackBuffer_);
    }

    return bufferRing_->getIoBuf(cqe);
  }

  bool isEOF(const struct io_uring_cqe* cqe) {
    return cqe->res == 0 && (bufferPool_ ? cqe->flags == 0 : true);
  }

  NetworkSocket fd_;
  IoUringRecvHandle* handle_;
  DestructorGuard handleGuard_;

  IoUringProvidedBufferRing* bufferRing_{nullptr};
  IoUringZeroCopyBufferPool* bufferPool_{nullptr};
  size_t recvLen_{0};
  std::unique_ptr<IOBuf> fallbackBuffer_;
};

/*
 * IoUringRecvHandle
 */

IoUringRecvHandle::UniquePtr IoUringRecvHandle::create(
    EventBase* evb,
    NetworkSocket fd,
    const SocketAddress& addr,
    IoUringRecvCallback* callback) {
  auto* backend = dynamic_cast<IoUringBackend*>(evb->getBackend());
  if (!backend) {
    return nullptr;
  }

  if (!backend->zcBufferPool() && !backend->hasBufferProvider()) {
    return nullptr;
  }

  auto handle = UniquePtr(new IoUringRecvHandle(backend, fd, addr, callback));
  return handle;
}

IoUringRecvHandle::IoUringRecvHandle(
    IoUringBackend* backend,
    NetworkSocket fd,
    const SocketAddress& addr,
    IoUringRecvCallback* callback)
    : backend_(backend),
      recvCallback_(callback),
      request_(
          RecvRequest::UniquePtr(new RecvRequest(backend, fd, addr, this))) {}

bool IoUringRecvHandle::update(uint16_t eventFlags) {
  if (!readEnabled_ && eventFlags & EventHandler::READ) {
    CHECK(!readEnabled_);
    readEnabled_ = true;
  } else if (readEnabled_ && !(eventFlags & EventHandler::READ)) {
    CHECK(readEnabled_);
    readEnabled_ = false;
  }

  return true;
}

void IoUringRecvHandle::submit(size_t maxSize) {
  CHECK(readEnabled_);
  CHECK(request_);
  // Some ReadCallbacks have a small peeking getReadBuffer() size, intended to
  // peek a few bytes in the socket. For these, issue a non-multishot recv.
  request_->setRecvLen(maxSize < kSmallRecvSize ? maxSize : 0);
  if (!request_->inFlight()) {
    backend_->submitSoon(*request_);
  }
}

bool IoUringRecvHandle::hasQueuedData() {
  return queuedReceivedData_ && !queuedReceivedData_->empty();
}

std::unique_ptr<IOBuf> IoUringRecvHandle::getQueuedData() {
  return std::move(queuedReceivedData_);
}

void IoUringRecvHandle::cancel() {
  if (request_->inFlight()) {
    request_->setEventBase(nullptr);
    backend_->cancel(request_.release());
  } else {
    request_.reset();
  }
  recvCallback_ = nullptr;
  backend_ = nullptr;
  return;
}

void IoUringRecvHandle::onRecvComplete(std::unique_ptr<IOBuf> data) {
  DestructorGuard dg(this);
  if (backend_ == nullptr) {
    CHECK(!request_);
    if (recvCallback_) {
      // DetachedReadCallback
      recvCallback_->recvSuccess(std::move(data));
    }
    return;
  }

  if (readEnabled_) {
    recvCallback_->recvSuccess(std::move(data));
  } else {
    if (!queuedReceivedData_) {
      queuedReceivedData_ = std::move(data);
    } else {
      queuedReceivedData_->appendToChain(std::move(data));
    }
  }

  if (backend_ && readEnabled_ && !request_->inFlight()) {
    backend_->submitSoon(*request_);
  }
}

void IoUringRecvHandle::onEnobufs() {
  CHECK(!request_->inFlight());
  if (backend_ && readEnabled_) {
    backend_->submitSoon(*request_);
  }
}

void IoUringRecvHandle::onRecvEOF() {
  DestructorGuard dg(this);
  if (recvCallback_) {
    recvCallback_->recvEOF();
  }
}

void IoUringRecvHandle::onRecvErr(int err) {
  DestructorGuard dg(this);
  if (recvCallback_) {
    recvCallback_->recvErr(err, nullptr);
  }
}

#else

class IoUringRecvHandle::RecvRequest : public DelayedDestruction {};

IoUringRecvHandle::UniquePtr IoUringRecvHandle::create(
    EventBase* /*evb*/,
    NetworkSocket /*fd*/,
    const SocketAddress& /*addr*/,
    IoUringRecvCallback* /*callback*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

IoUringRecvHandle::IoUringRecvHandle(
    IoUringBackend* /*backend*/,
    NetworkSocket /*fd*/,
    const SocketAddress& /*addr*/,
    IoUringRecvCallback* /*callback*/) {
  (void)backend_;
  (void)recvCallback_;
  (void)request_;
  (void)queuedReceivedData_;
  (void)readEnabled_;
}

bool IoUringRecvHandle::update(uint16_t /*eventFlags*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::submit(size_t /*maxSize*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

bool IoUringRecvHandle::hasQueuedData() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

std::unique_ptr<IOBuf> IoUringRecvHandle::getQueuedData() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::cancel() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::onRecvComplete(std::unique_ptr<IOBuf> /*data*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::onEnobufs() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::onRecvEOF() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringRecvHandle::onRecvErr(int /*err*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

#endif

} // namespace folly
