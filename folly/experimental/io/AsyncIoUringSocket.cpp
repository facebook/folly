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

#include <folly/Conv.h>
#include <folly/experimental/io/AsyncIoUringSocket.h>
#include <folly/experimental/io/IoUringEventBaseLocal.h>
#include <folly/io/Cursor.h>
#include <folly/io/async/AsyncSocket.h>

#if __has_include(<liburing.h>)

namespace fsp = folly::portability::sockets;

namespace folly {

namespace {

AsyncSocket* getAsyncSocket(AsyncTransport::UniquePtr const& o) {
  auto* raw = o->getUnderlyingTransport<folly::AsyncSocket>();
  if (!raw) {
    throw std::runtime_error("need to take a AsyncSocket");
  }
  return raw;
}

int ensureSocketReturnCode(int x, char const* message) {
  if (x >= 0) {
    return x;
  }
  auto errnoCopy = errno;
  throw AsyncSocketException(
      AsyncSocketException::INTERNAL_ERROR, message, errnoCopy);
}

NetworkSocket makeConnectSocket(SocketAddress const& peerAddress) {
  int fd = ensureSocketReturnCode(
      ::socket(peerAddress.getFamily(), SOCK_STREAM, 0),
      "failed to create socket");
  ensureSocketReturnCode(fcntl(fd, F_SETFD, FD_CLOEXEC), "set cloexec");

  // copied from folly::AsyncSocket, default enable TCP_NODELAY
  int nodelay = 1;
  ensureSocketReturnCode(
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)),
      "set nodelay");
  return NetworkSocket{fd};
}

} // namespace

AsyncIoUringSocket::AsyncIoUringSocket(
    folly::AsyncSocket* other, IoUringBackend* backend)
    : AsyncIoUringSocket(other->getEventBase(), backend) {
  preReceivedData_ = other->takePreReceivedData();
  DVLOG(5) << "got pre-received " << preReceivedData_.get();
  setFd(other->detachNetworkSocket());
}

AsyncIoUringSocket::AsyncIoUringSocket(
    AsyncTransport::UniquePtr other, IoUringBackend* backend)
    : AsyncIoUringSocket(getAsyncSocket(other), backend) {
  if (!preReceivedData_) {
    preReceivedData_ = other->takePreReceivedData();
    DVLOG(5) << "got pre-received " << preReceivedData_.get();
  }
}

AsyncIoUringSocket::AsyncIoUringSocket(EventBase* evb, IoUringBackend* backend)
    : evb_(evb), backend_(backend) {
  if (!backend_) {
    backend_ = IoUringEventBaseLocal::try_get(evb);
  }
  if (!backend_) {
    backend_ = dynamic_cast<IoUringBackend*>(evb_->getBackend());
  }
  if (!backend_) {
    throw std::runtime_error("need to take a IoUringBackend event base");
  }

  if (!backend_->bufferProvider()) {
    throw std::runtime_error("require a IoUringBackend with a buffer provider");
  }
  readSqe_ = std::make_unique<ReadSqe>(this, backend_->bufferProvider());
  supportsMultishotRecv_ = backend_->kernelSupportsRecvmsgMultishot();
}

AsyncIoUringSocket::~AsyncIoUringSocket() {
  DVLOG(3) << "~AsyncIoUringSocket() " << this;

  // this is a bit unnecesary if we are already closed, but proper state
  // tracking is coming later and will be easier to handle then
  closeNow();

  if (fdRegistered_ && !backend_->unregisterFd(fdRegistered_)) {
    LOG(ERROR) << "Bad fd unregister";
  }
  fdRegistered_ = nullptr;

  // cancel outstanding
  if (readSqe_->inFlight()) {
    DVLOG(3) << "cancel reading " << readSqe_.get();
    backend_->cancel(readSqe_.release());
  }
  if (preReadSqe_->inFlight()) {
    DVLOG(3) << "cancel prereading " << preReadSqe_.get();
    backend_->cancel(preReadSqe_.release());
  }
  if (closeSqe_->inFlight()) {
    DVLOG(3) << "cancel close " << closeSqe_.get();
    closeSqe_
        ->markCancelled(); // still need to actually close it and it has no data
    closeSqe_.release();
  }
  if (connectSqe_ && connectSqe_->inFlight()) {
    DVLOG(3) << "cancel connect " << connectSqe_.get();
    backend_->cancel(connectSqe_.release());
  }

  DVLOG(2) << "~AsyncIoUringSocket() " << this << " have active "
           << writeSqeActive_ << " queue=" << writeSqeQueue_.size();

  if (writeSqeActive_) {
    backend_->cancel(writeSqeActive_);
  }

  while (!writeSqeQueue_.empty()) {
    WriteSqe* w = &writeSqeQueue_.front();
    CHECK(!w->inFlight());
    writeSqeQueue_.pop_front();
    delete w;
  }
}

bool AsyncIoUringSocket::supports(EventBase* eb) {
  if (IoUringEventBaseLocal::try_get(eb)) {
    return true;
  }
  IoUringBackend* io = dynamic_cast<IoUringBackend*>(eb->getBackend());
  return !!io;
}

void AsyncIoUringSocket::connect(
    AsyncSocket::ConnectCallback* callback,
    const folly::SocketAddress& address,
    std::chrono::milliseconds timeout) noexcept {
  evb_->dcheckIsInEventBaseThread();
  DestructorGuard dg(this);
  connectTimeout_ = timeout;
  connectEndTime_ = connectStartTime_ = std::chrono::steady_clock::now();
  if (!connectSqe_) {
    connectSqe_ = std::make_unique<ConnectSqe>(this);
  }
  if (connectSqe_->inFlight()) {
    callback->connectErr(AsyncSocketException(
        AsyncSocketException::NOT_OPEN, "connection in flight", -1));
    return;
  }
  if (fd_ != NetworkSocket{}) {
    callback->connectErr(AsyncSocketException(
        AsyncSocketException::NOT_OPEN, "connection is connected", -1));
    return;
  }
  connectCallback_ = callback;
  peerAddress_ = address;

  setFd(makeConnectSocket(address));
  connectCallback_->preConnect(fd_);
  if (connectTimeout_.count() > 0) {
    if (!connectSqe_->scheduleTimeout(connectTimeout_)) {
      connectCallback_->connectErr(AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "failed to schedule connect timeout"));
      connectCallback_ = nullptr;
      connectSqe_.reset();
      return;
    }
  }
  backend_->submit(*connectSqe_);
}

void AsyncIoUringSocket::processConnectSubmit(
    struct io_uring_sqe* sqe, sockaddr_storage& storage) {
  auto len = peerAddress_.getAddress(&storage);
  io_uring_prep_connect(sqe, usedFd_, (struct sockaddr*)&storage, len);
  sqe->flags |= mbFixedFileFlags_;
}

void AsyncIoUringSocket::processConnectResult(int i) {
  connectSqe_.reset();
  connectEndTime_ = std::chrono::steady_clock::now();
  if (i == 0) {
    connectCallback_->connectSuccess();
  } else {
    connectCallback_->connectErr(AsyncSocketException(
        AsyncSocketException::NOT_OPEN, "connect failed", -i));
  }
  connectCallback_ = nullptr;
}

void AsyncIoUringSocket::processConnectTimeout() {
  connectSqe_.reset();
  connectEndTime_ = std::chrono::steady_clock::now();
  connectCallback_->connectErr(
      AsyncSocketException(AsyncSocketException::TIMED_OUT, "timeout"));
  connectCallback_ = nullptr;
}

inline bool AsyncIoUringSocket::readCallbackUseIoBufs() const {
  return readCallback_ && readCallback_->isBufferMovable();
}

void AsyncIoUringSocket::invalidState(ReadCallback* callback) {
  DVLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_
           << "): setReadCallback(" << callback << ") called in invalid state ";

  AsyncSocketException ex(
      AsyncSocketException::NOT_OPEN,
      "setReadCallback() called  io_uringwith socket in "
      "invalid state");
  if (callback) {
    callback->readErr(ex);
  }
}

void AsyncIoUringSocket::setReadCB(ReadCallback* callback) {
  evb_->dcheckIsInEventBaseThread();
  DVLOG(5) << "AsyncIoUringSocket::setReadCB() this=" << this
           << " cb=" << callback << " count=" << setReadCbCount_ << " movable="
           << (callback && callback->isBufferMovable() ? "YES" : "NO")
           << " inflight=" << readSqe_->inFlight() << " good_=" << good_;
  if (callback == readCallback_) {
    // copied from AsyncSocket
    DVLOG(9) << "cb the same";
    return;
  }
  setReadCbCount_++;
  readCallback_ = callback;
  if (!readCallback_) {
    return;
  }
  if (!good_) {
    readCallback_ = nullptr;
    invalidState(callback);
    return;
  }
  if (preReceivedData_) {
    DVLOG(9) << "submit preread";
    backend_->submit(*preReadSqe_);
  } else if (!readSqe_->inFlight()) {
    submitRead();
  }

  // else if (readSqe_->inFlight()) implies the the read is queued, and will
  // complete at some point - it will pick up the new callback
}

void AsyncIoUringSocket::submitRead(bool now) {
  DVLOG(9) << "AsyncIoUringSocket::submitRead " << now
           << " sqe=" << readSqe_.get();
  if (now) {
    backend_->submitNow(*readSqe_);
  } else {
    backend_->submitSoon(*readSqe_);
  }
}

void AsyncIoUringSocket::readProcessSubmit(
    struct io_uring_sqe* sqe,
    IoUringBackend::ProvidedBufferProviderBase* bufferProvider,
    size_t* maxSize,
    bool* usedBufferProvider) noexcept {
  *usedBufferProvider = false;
  if (!readCallback_) {
    VLOG(2) << "readProcessSubmit with no callback?";
    tmpBuffer_ = IOBuf::create(2000);
    *maxSize = tmpBuffer_->tailroom();
    ::io_uring_prep_recv(sqe, usedFd_, tmpBuffer_->writableTail(), *maxSize, 0);
  } else {
    if (readCallbackUseIoBufs()) {
      if (bufferProvider->available()) {
        *maxSize = bufferProvider->sizePerBuffer();

        size_t used_len;
        unsigned int ioprio_flags;
        if (supportsMultishotRecv_) {
          // #define IORING_RECV_MULTISHOT	(1U << 1)
          ioprio_flags = (1U << 1);
          used_len = 0;
        } else {
          ioprio_flags = 0;
          used_len = *maxSize;
        }

        ::io_uring_prep_recv(sqe, usedFd_, nullptr, used_len, 0);
        sqe->buf_group = bufferProvider->gid();
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->ioprio |= ioprio_flags;
        *usedBufferProvider = true;
        DVLOG(9)
            << "AsyncIoUringSocket::readProcessSubmit bufferprovider multishot";
      } else {
        tmpBuffer_ = IOBuf::create(16000);
        *maxSize = tmpBuffer_->tailroom();
        VLOG(2) << "UseProvidedBuffers slow path starting with " << *maxSize
                << " bytes ";
        ::io_uring_prep_recv(
            sqe, usedFd_, tmpBuffer_->writableTail(), *maxSize, 0);
      }
    } else {
      void* buf;
      readCallback_->getReadBuffer(&buf, maxSize);
      tmpBuffer_ = IOBuf::create(*maxSize);
      ::io_uring_prep_recv(
          sqe, usedFd_, tmpBuffer_->writableTail(), *maxSize, 0);
      DVLOG(9)
          << "AsyncIoUringSocket::readProcessSubmit  tmp buffer using size "
          << *maxSize;
    }

    sqe->flags |= mbFixedFileFlags_;
    DVLOG(5) << "readProcessSubmit " << this << " fd=" << fd_
             << " reg=" << usedFd_ << " cb=" << readCallback_
             << " size=" << *maxSize;
  }
}

void AsyncIoUringSocket::appendPreReceive(
    std::unique_ptr<IOBuf> iobuf) noexcept {
  if (preReceivedData_) {
    preReceivedData_->appendToChain(std::move(iobuf));
  } else {
    preReceivedData_ = std::move(iobuf);
  }
}

void AsyncIoUringSocket::sendReadBuf(std::unique_ptr<IOBuf> buf) noexcept {
  while (readCallback_) {
    if (readCallback_->isBufferMovable()) {
      readCallback_->readBufferAvailable(std::move(buf));
      return;
    }

    auto* rcb_was = readCallback_;
    size_t sz;
    void* b;
    size_t total = 0;
    io::Cursor cursor(buf.get());
    do {
      readCallback_->getReadBuffer(&b, &sz);
      size_t took = cursor.pullAtMost(b, sz);
      readCallback_->readDataAvailable(took);
      if (cursor.isAtEnd()) {
        return;
      }
      total += took;
    } while (readCallback_ == rcb_was);

    // annoying path, have to update buf
    while (buf->length() < total) {
      total -= buf->length();
      buf = std::move(buf)->unlink();
    }
    buf->trimStart(total);
  }
  appendPreReceive(std::move(buf));
}

void AsyncIoUringSocket::readCallback(
    int res,
    uint32_t flags,
    size_t maxSize,
    IoUringBackend::ProvidedBufferProviderBase* bufferProvider) noexcept {
  DestructorGuard dg(this);
  DVLOG(5) << "AsyncIoUringSocket::readCallback() this=" << this
           << " cb=" << readCallback_ << " sqe=" << readSqe_.get()
           << " res=" << res << " max=" << maxSize
           << " inflight=" << readSqe_->inFlight()
           << " has_buffer=" << !!(flags & IORING_CQE_F_BUFFER)
           << " bytes_received=" << bytesReceived_;
  auto buffer_guard = makeGuard([&] {
    if (flags & IORING_CQE_F_BUFFER) {
      DCHECK(bufferProvider);
      if (bufferProvider) {
        bufferProvider->unusedBuf(flags >> 16, res);
      }
    }
  });
  if (!readCallback_) {
    if (res == -ENOBUFS || res == -ECANCELED) {
      // ignore
    } else if (res <= 0) {
      // EOF?
      good_ = false;
    } else if (res > 0 && bufferProvider) {
      // must take the buffer
      appendPreReceive(bufferProvider->getIoBuf(flags >> 16, res));
      buffer_guard.dismiss();
    }
  } else {
    if (res == 0) {
      good_ = false;
      readCallback_->readEOF();
    } else if (res == -ENOBUFS) {
      if (bufferProvider) {
        // urgh, resubmit and let submit logic deal with the fact
        // we have no more buffers
        bufferProvider->enobuf();
      }
      submitRead();
    } else if (res < 0) {
      // ERROR?
      good_ = false;
      error_ = true;
      AsyncSocketException::AsyncSocketExceptionType err;
      std::string error;
      switch (res) {
        case -EBADF:
          err = AsyncSocketException::NOT_OPEN;
          error = "AsyncIoUringSocket: read error: EBADF";
          break;
        default:
          err = AsyncSocketException::UNKNOWN;
          error = to<std::string>(
              "AsyncIoUringSocket: read error: ",
              folly::errnoStr(-res),
              ": (",
              res,
              ")");
          break;
      };
      readCallback_->readErr(AsyncSocketException(err, std::move(error)));
    } else {
      uint64_t const cb_was = setReadCbCount_;
      bytesReceived_ += res;
      if (bufferProvider) {
        sendReadBuf(bufferProvider->getIoBuf(flags >> 16, res));
        buffer_guard.dismiss();
      } else {
        // slow path as must have run out of buffers
        // or maybe the callback does not support whole buffers
        DCHECK(tmpBuffer_);
        tmpBuffer_->append(res);
        DVLOG(2) << "UseProvidedBuffers slow path completed " << res;
        sendReadBuf(std::move(tmpBuffer_));
      }
      // callback may have changed now!
      if (setReadCbCount_ == cb_was && !readSqe_->inFlight()) {
        submitRead(maxSize == (size_t)res);
      }
    }
  }
}

void AsyncIoUringSocket::preReadCallback() noexcept {
  DestructorGuard dg(this);
  DVLOG(5) << "AsyncIoUringSocket::preReadCallback() " << this
           << " data=" << preReceivedData_.get();

  if (!readCallback_ || !preReceivedData_) {
    return;
  }

  sendReadBuf(std::move(preReceivedData_));

  DVLOG(5) << "AsyncIoUringSocket::preReadCallback() done " << this;
  // only submit if nothing in flight:
  if (!preReadSqe_->inFlight() && !readSqe_->inFlight()) {
    submitRead();
  }
}

AsyncIoUringSocket::WriteSqe::WriteSqe(
    AsyncIoUringSocket* parent,
    WriteCallback* callback,
    std::unique_ptr<IOBuf>&& buf,
    WriteFlags flags)
    : parent_(parent),
      callback_(callback),
      buf_(std::move(buf)),
      flags_(flags),
      totalLength_(0) {
  IOBuf const* p = buf_.get();
  do {
    if (auto l = p->length(); l > 0) {
      iov_.emplace_back();
      iov_.back().iov_base = const_cast<uint8_t*>(p->data());
      iov_.back().iov_len = l;
      totalLength_ += l;
    }
    p = p->next();
  } while (p != buf_.get());

  msg_.msg_iov = iov_.data();
  msg_.msg_iovlen = iov_.size();
  msg_.msg_name = nullptr;
  msg_.msg_namelen = 0;
  msg_.msg_control = nullptr;
  msg_.msg_controllen = 0;
  msg_.msg_flags = 0;
}

int AsyncIoUringSocket::WriteSqe::sendMsgFlags() const {
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

void AsyncIoUringSocket::WriteSqe::processSubmit(
    struct io_uring_sqe* sqe) noexcept {
  DVLOG(5) << "write sqe submit " << this << " iovs=" << msg_.msg_iovlen
           << " length=" << totalLength_ << " ptr=" << msg_.msg_iov;
  ::io_uring_prep_sendmsg(sqe, parent_->usedFd_, &msg_, sendMsgFlags());
  sqe->flags |= parent_->mbFixedFileFlags_;
}

void AsyncIoUringSocket::writeDone() noexcept {
  DVLOG(5) << "AsyncIoUringSocket::writeDone queue=" << writeSqeQueue_.size()
           << " active=" << writeSqeActive_;

  if (writeTimeoutTime_.count() > 0) {
    writeTimeout_.cancelTimeout();
  }
  if (writeSqeActive_ || writeSqeQueue_.empty()) {
    return;
  }
  writeSqeActive_ = &writeSqeQueue_.front();
  writeSqeQueue_.pop_front();
  doSubmitWrite();
}

void AsyncIoUringSocket::doSubmitWrite() noexcept {
  DCHECK(writeSqeActive_);
  backend_->submitSoon(*writeSqeActive_);
  if (writeTimeoutTime_.count() > 0) {
    startSendTimeout();
  }
}

void AsyncIoUringSocket::doReSubmitWrite() noexcept {
  DCHECK(writeSqeActive_);
  backend_->submitSoon(*writeSqeActive_);
  // do not update the send timeout for partial writes
}

void AsyncIoUringSocket::WriteSqe::callback(int res, uint32_t flags) noexcept {
  DVLOG(5) << "write sqe callback " << this << " res=" << res
           << " flags=" << flags << " iovStart=" << iov_.size()
           << " iovRemaining=" << msg_.msg_iovlen << " length=" << totalLength_;
  DestructorGuard dg(parent_);

  if (res > 0 && (size_t)res < totalLength_) {
    // todo clean out the iobuf
    size_t toRemove = res;
    parent_->bytesWritten_ += res;
    totalLength_ -= toRemove;
    while (toRemove) {
      if (msg_.msg_iov->iov_len > toRemove) {
        msg_.msg_iov->iov_len -= toRemove;
        msg_.msg_iov->iov_base = ((char*)msg_.msg_iov->iov_base) + toRemove;
        toRemove = 0;
      } else {
        toRemove -= msg_.msg_iov->iov_len;
        DCHECK(msg_.msg_iovlen > 1);
        ++msg_.msg_iov;
        --msg_.msg_iovlen;
      }
    }
    // partial write
    parent_->doReSubmitWrite();
  } else {
    buf_.reset();
    if (callback_) {
      if (res >= 0) {
        // todo
        parent_->bytesWritten_ += res;
        callback_->writeSuccess();
      } else if (res < 0) {
        DVLOG(2) << "write error! " << res;
        callback_->writeErr(
            0,
            AsyncSocketException(AsyncSocketException::UNKNOWN, "write error"));
      }
    }
    parent_->writeSqeActive_ = nullptr;
    parent_->writeDone();
    delete this;
  }
}

void AsyncIoUringSocket::failWrite(const AsyncSocketException& ex) {
  if (!writeSqeActive_) {
    return;
  }
  DestructorGuard dg(this);
  writeSqeActive_->callback_->writeErr(0, ex);
  backend_->cancel(writeSqeActive_);
  writeSqeActive_ = nullptr;
  writeDone();
}

void AsyncIoUringSocket::write(
    WriteCallback* callback, const void* buff, size_t n, WriteFlags wf) {
  // pretty sure that buff cannot change until the write completes
  writeChain(callback, IOBuf::wrapBuffer(buff, n), wf);
}

void AsyncIoUringSocket::writev(
    WriteCallback* callback, const iovec* iov, size_t n, WriteFlags wf) {
  if (n == 0) {
    callback->writeSuccess();
    return;
  }
  auto first = IOBuf::wrapBuffer(iov[0].iov_base, iov[0].iov_len);
  for (size_t i = 1; i < n; i++) {
    first->appendToChain(IOBuf::wrapBuffer(iov[i].iov_base, iov[i].iov_len));
  }
  writeChain(callback, std::move(first), wf);
}

void AsyncIoUringSocket::writeChain(
    WriteCallback* callback, std::unique_ptr<IOBuf>&& buf, WriteFlags flags) {
  WriteSqe* w = new WriteSqe(this, callback, std::move(buf), flags);
  if (writeSqeActive_) {
    writeSqeQueue_.push_back(*w);
    DVLOG(5) << "enquque " << w << " as have active. queue now "
             << writeSqeQueue_.size();
  } else {
    writeSqeActive_ = w;
    doSubmitWrite();
  }
}

void AsyncIoUringSocket::closeProcessSubmit(struct io_uring_sqe* sqe) {
  if (fdRegistered_) {
    ::io_uring_prep_close_direct(sqe, fdRegistered_->idx_);
  } else {
    ::io_uring_prep_close(sqe, fd_.toFd());
  }
}

void AsyncIoUringSocket::closeWithReset() {
  // copied from AsyncSocket
  // Enable SO_LINGER, with the linger timeout set to 0.
  // This will trigger a TCP reset when we close the socket.

  struct linger optLinger = {1, 0};
  if (::setsockopt(
          fd_.toFd(), SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger)) !=
      0) {
    VLOG(2) << "AsyncIoUringSocket::closeWithReset(): "
            << "error setting SO_LINGER on " << fd_ << ": errno=" << errno;
  }

  // Then let closeNow() take care of the rest
  closeNow();
}

void AsyncIoUringSocket::close() {
  closeNow();
}

void AsyncIoUringSocket::closeNow() {
  DestructorGuard dg(this);
  DVLOG(2) << "AsyncIoUringSocket::closeNow() this=" << this << " fd_=" << fd_
           << " reg=" << fdRegistered_;
  good_ = false;
  if (fdRegistered_) {
    // we cannot trust that close will actually end the socket, as a
    // registered socket may be held onto for a while. So always do a shutdown
    // in case.
    ::shutdown(fd_.toFd(), SHUT_RDWR);
  }

  if (!closeSqe_->inFlight()) {
    backend_->submitNow(*closeSqe_);
  }
  if (readCallback_) {
    ReadCallback* callback = readCallback_;
    readCallback_ = nullptr;
    callback->readEOF();
  }
}

void AsyncIoUringSocket::sendTimeoutExpired() {
  if (connectSqe_) {
    // reused the connect sqe
    return;
  }
  failWrite(
      AsyncSocketException(AsyncSocketException::TIMED_OUT, "write timed out"));
}

void AsyncIoUringSocket::startSendTimeout() {
  if (!writeTimeout_.scheduleTimeout(writeTimeoutTime_)) {
    failWrite(AsyncSocketException(
        AsyncSocketException::INTERNAL_ERROR,
        "failed to reschedule send timeout in startSendTimeout"));
  }
}

void AsyncIoUringSocket::setSendTimeout(uint32_t ms) {
  writeTimeoutTime_ = std::chrono::milliseconds{ms};
  if (evb_) {
    evb_->dcheckIsInEventBaseThread();
  }

  if (!writeSqeActive_) {
    return;
  }
  // If we are currently pending on write requests, immediately update
  // writeTimeout_ with the new value.
  if (writeTimeoutTime_.count() > 0) {
    startSendTimeout();
  } else {
    writeTimeout_.cancelTimeout();
  }
}

void AsyncIoUringSocket::getLocalAddress(SocketAddress* address) const {
  if (!localAddress_.isInitialized()) {
    localAddress_.setFromLocalAddress(fd_);
  }
  *address = localAddress_;
}

void AsyncIoUringSocket::getPeerAddress(SocketAddress* address) const {
  if (!peerAddress_.isInitialized()) {
    peerAddress_.setFromPeerAddress(fd_);
  }
  *address = peerAddress_;
}

void AsyncIoUringSocket::setFd(NetworkSocket ns) {
  fd_ = ns;
  try {
    if (!backend_->kernelHasNonBlockWriteFixes()) {
      // If the kernel doesnt have the fixes we have to disable the nonblock
      // flag It will still be NONBLOCK as long as it goes through io_uring, but
      // if we leave the flag then IO_URING will spin on some ops.
      int flags =
          ensureSocketReturnCode(fcntl(ns.toFd(), F_GETFL, 0), "get flags");
      flags = flags & ~O_NONBLOCK;
      ensureSocketReturnCode(fcntl(ns.toFd(), F_SETFL, flags), "set flags");
    }

    auto start = std::chrono::steady_clock::now();
    fdRegistered_ = backend_->registerFd(fd_.toFd());
    auto end = std::chrono::steady_clock::now();
    if (end - start > std::chrono::milliseconds(1)) {
      LOG(INFO) << "registering fd took "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       end - start)
                       .count()
                << "us";
    }
    if (fdRegistered_) {
      usedFd_ = fdRegistered_->idx_;
      mbFixedFileFlags_ = IOSQE_FIXED_FILE;
    } else {
      usedFd_ = fd_.toFd();
      VLOG(1) << "unable to register fd: " << fd_.toFd();
    }
  } catch (...) {
    ::close(ns.toFd());
    throw;
  }
}

} // namespace folly

#endif
