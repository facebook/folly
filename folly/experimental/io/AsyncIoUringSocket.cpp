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
  setPreReceivedData(other->takePreReceivedData());
  setFd(other->detachNetworkSocket());
}

AsyncIoUringSocket::AsyncIoUringSocket(
    AsyncTransport::UniquePtr other, IoUringBackend* backend)
    : AsyncIoUringSocket(getAsyncSocket(other), backend) {
  setPreReceivedData(other->takePreReceivedData());
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
  readSqe_ = ReadSqe::UniquePtr(new ReadSqe(this, backend_));
}

AsyncIoUringSocket::ReadSqe::ReadSqe(
    AsyncIoUringSocket* parent, IoUringBackend* backend)
    : bufferProvider_(backend->bufferProvider()), parent_(parent) {
  supportsMultishotRecv_ = backend->kernelSupportsRecvmsgMultishot();
}

AsyncIoUringSocket::~AsyncIoUringSocket() {
  DVLOG(3) << "~AsyncIoUringSocket() " << this;

  // this is a bit unnecesary if we are already closed, but proper state
  // tracking is coming later and will be easier to handle then
  closeNow();

  // cancel outstanding
  if (readSqe_->inFlight()) {
    DVLOG(3) << "cancel reading " << readSqe_.get();
    backend_->cancel(readSqe_.release());
  }
  if (closeSqe_ && closeSqe_->inFlight()) {
    LOG_EVERY_N(WARNING, 100) << " closeSqe_ still in flight";
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
    std::chrono::milliseconds timeout,
    SocketOptionMap const& options,
    const folly::SocketAddress& bindAddr,
    const std::string& ifName) noexcept {
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

  {
    auto result =
        applySocketOptions(fd_, options, SocketOptionKey::ApplyPos::PRE_BIND);
    if (result != 0) {
      callback->connectErr(AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "failed to set socket option",
          result));
      return;
    }
  }

  // bind the socket to the interface
  if (!ifName.empty() &&
      setSockOpt(
          SOL_SOCKET, SO_BINDTODEVICE, ifName.c_str(), ifName.length())) {
    auto errnoCopy = errno;
    callback->connectErr(AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "failed to bind to device: " + ifName,
        errnoCopy));
    return;
  }

  // bind the socket
  if (bindAddr != anyAddress()) {
    sockaddr_storage addrStorage;
    auto saddr = reinterpret_cast<sockaddr*>(&addrStorage);

    int one = 1;
    if (setSockOpt(SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
      auto errnoCopy = errno;
      callback->connectErr(AsyncSocketException(
          AsyncSocketException::NOT_OPEN,
          "failed to setsockopt prior to bind on " + bindAddr.describe(),
          errnoCopy));
      return;
    }

    bindAddr.getAddress(&addrStorage);

    if (::bind(fd_.toFd(), saddr, bindAddr.getActualSize()) != 0) {
      auto errnoCopy = errno;
      callback->connectErr(AsyncSocketException(
          AsyncSocketException::NOT_OPEN,
          "failed to bind to async socket: " + bindAddr.describe(),
          errnoCopy));
      return;
    }
  }

  {
    auto result =
        applySocketOptions(fd_, options, SocketOptionKey::ApplyPos::POST_BIND);
    if (result != 0) {
      callback->connectErr(AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "failed to set socket option",
          result));
      return;
    }
  }

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

inline bool AsyncIoUringSocket::ReadSqe::readCallbackUseIoBufs() const {
  return readCallback_ && readCallback_->isBufferMovable();
}

void AsyncIoUringSocket::readEOF() {
  good_ = false;
}

void AsyncIoUringSocket::readError() {
  good_ = false;
  error_ = true;
}

void AsyncIoUringSocket::setReadCB(ReadCallback* callback) {
  evb_->dcheckIsInEventBaseThread();
  readSqe_->setReadCallback(callback);
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

void AsyncIoUringSocket::ReadSqe::invalidState(ReadCallback* callback) {
  DVLOG(4) << "AsyncSocket(this=" << this << "): setReadCallback(" << callback
           << ") called in invalid state ";

  AsyncSocketException ex(
      AsyncSocketException::NOT_OPEN,
      "setReadCallback() called  io_uringwith socket in "
      "invalid state");
  if (callback) {
    callback->readErr(ex);
  }
}

void AsyncIoUringSocket::ReadSqe::setReadCallback(ReadCallback* callback) {
  DVLOG(5) << "AsyncIoUringSocket::setReadCB() this=" << this
           << " cb=" << callback << " count=" << setReadCbCount_ << " movable="
           << (callback && callback->isBufferMovable() ? "YES" : "NO")
           << " inflight=" << inFlight() << " good_=" << parent_->good();
  if (callback == readCallback_) {
    // copied from AsyncSocket
    DVLOG(9) << "cb the same";
    return;
  }
  setReadCbCount_++;
  readCallback_ = callback;
  if (!callback) {
    return;
  }
  if (!parent_->good()) {
    readCallback_ = nullptr;
    invalidState(callback);
    return;
  }

  // callback may change after these so make sure to check

  if (readCallback_ && preReceivedData_) {
    sendReadBuf(std::move(preReceivedData_), preReceivedData_);
  }

  if (readCallback_ && queuedReceivedData_) {
    sendReadBuf(std::move(queuedReceivedData_), queuedReceivedData_);
  }

  if (readCallback_ && !inFlight()) {
    parent_->submitRead();
  }
}

void AsyncIoUringSocket::ReadSqe::callback(int res, uint32_t flags) noexcept {
  DVLOG(5) << "AsyncIoUringSocket::ReadSqe::readCallback() this=" << this
           << " parent=" << parent_ << " cb=" << readCallback_ << " res=" << res
           << " max=" << maxSize_ << " inflight=" << inFlight()
           << " has_buffer=" << !!(flags & IORING_CQE_F_BUFFER)
           << " bytes_received=" << bytesReceived_;
  DestructorGuard dg(this);
  auto buffer_guard = makeGuard([&] {
    if (flags & IORING_CQE_F_BUFFER) {
      DCHECK(lastUsedBufferProvider_);
      if (lastUsedBufferProvider_) {
        lastUsedBufferProvider_->unusedBuf(flags >> 16);
      }
    }
  });
  if (!readCallback_) {
    if (res == -ENOBUFS || res == -ECANCELED) {
      // ignore
    } else if (res <= 0) {
      // EOF?
      parent_->readEOF();
    } else if (res > 0 && lastUsedBufferProvider_) {
      // must take the buffer
      appendReadData(
          lastUsedBufferProvider_->getIoBuf(flags >> 16, res),
          queuedReceivedData_);
      buffer_guard.dismiss();
    }
  } else {
    if (res == 0) {
      parent_->readEOF();
      readCallback_->readEOF();
    } else if (res == -ENOBUFS) {
      if (lastUsedBufferProvider_) {
        // urgh, resubmit and let submit logic deal with the fact
        // we have no more buffers
        lastUsedBufferProvider_->enobuf();
      }
      parent_->submitRead();
    } else if (res < 0) {
      // ERROR?
      parent_->readError();
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
      if (lastUsedBufferProvider_) {
        sendReadBuf(
            lastUsedBufferProvider_->getIoBuf(flags >> 16, res),
            queuedReceivedData_);
        buffer_guard.dismiss();
      } else {
        // slow path as must have run out of buffers
        // or maybe the callback does not support whole buffers
        DCHECK(tmpBuffer_);
        tmpBuffer_->append(res);
        DVLOG(2) << "UseProvidedBuffers slow path completed " << res;
        sendReadBuf(std::move(tmpBuffer_), queuedReceivedData_);
      }
      // callback may have changed now, or we may not have a parent!
      if (parent_ && setReadCbCount_ == cb_was && !inFlight()) {
        parent_->submitRead(maxSize_ == (size_t)res);
      }
    }
  }
}

void AsyncIoUringSocket::ReadSqe::processSubmit(
    struct io_uring_sqe* sqe) noexcept {
  lastUsedBufferProvider_ = nullptr;
  if (!readCallback_) {
    VLOG(2) << "readProcessSubmit with no callback?";
    tmpBuffer_ = IOBuf::create(2000);
    maxSize_ = tmpBuffer_->tailroom();
    ::io_uring_prep_recv(sqe, usedFd_, tmpBuffer_->writableTail(), maxSize_, 0);
  } else {
    if (readCallbackUseIoBufs()) {
      if (bufferProvider_->available()) {
        lastUsedBufferProvider_ = bufferProvider_;
        maxSize_ = lastUsedBufferProvider_->sizePerBuffer();

        size_t used_len;
        unsigned int ioprio_flags;
        if (supportsMultishotRecv_) {
          ioprio_flags = IORING_RECV_MULTISHOT;
          used_len = 0;
        } else {
          ioprio_flags = 0;
          used_len = maxSize_;
        }

        ::io_uring_prep_recv(sqe, usedFd_, nullptr, used_len, 0);
        sqe->buf_group = lastUsedBufferProvider_->gid();
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->ioprio |= ioprio_flags;
        DVLOG(9)
            << "AsyncIoUringSocket::readProcessSubmit bufferprovider multishot";
      } else {
        size_t hint = 16000; // todo: get from readCallback

        tmpBuffer_ = IOBuf::create(hint);
        maxSize_ = tmpBuffer_->tailroom();
        VLOG(2) << "UseProvidedBuffers slow path starting with " << maxSize_
                << " bytes ";
        ::io_uring_prep_recv(
            sqe, usedFd_, tmpBuffer_->writableTail(), maxSize_, 0);
      }
    } else {
      void* buf;
      readCallback_->getReadBuffer(&buf, &maxSize_);
      maxSize_ = std::min<size_t>(maxSize_, 2048);
      tmpBuffer_ = IOBuf::create(maxSize_);
      ::io_uring_prep_recv(
          sqe, usedFd_, tmpBuffer_->writableTail(), maxSize_, 0);
      DVLOG(9)
          << "AsyncIoUringSocket::readProcessSubmit  tmp buffer using size "
          << maxSize_;
    }

    DVLOG(5) << "readProcessSubmit " << this << " reg=" << usedFd_
             << " cb=" << readCallback_ << " size=" << maxSize_;
  }
}

void AsyncIoUringSocket::ReadSqe::sendReadBuf(
    std::unique_ptr<IOBuf> buf, std::unique_ptr<IOBuf>& overflow) noexcept {
  while (readCallback_) {
    if (FOLLY_LIKELY(readCallback_->isBufferMovable())) {
      readCallback_->readBufferAvailable(std::move(buf));
      return;
    }
    auto* rcb_was = readCallback_;
    size_t sz;
    void* b;

    do {
      readCallback_->getReadBuffer(&b, &sz);
      size_t took = std::min<size_t>(sz, buf->length());
      VLOG(1) << "... inner sz=" << sz << "  len=" << buf->length();

      if (FOLLY_LIKELY(took)) {
        memcpy(b, buf->data(), took);

        readCallback_->readDataAvailable(took);
        if (buf->length() == took) {
          buf = buf->pop();
          if (!buf) {
            return;
          }
        } else {
          buf->trimStart(took);
        }
      } else {
        VLOG(1) << "Bad!";
        // either empty buffer or the readcallback is bad.
        // assume empty buffer for simplicity
        buf = buf->pop();
        if (!buf) {
          return;
        }
      }
    } while (readCallback_ == rcb_was);
  }
  appendReadData(std::move(buf), overflow);
}

std::unique_ptr<IOBuf> AsyncIoUringSocket::ReadSqe::takePreReceivedData() {
  return std::move(preReceivedData_);
}

void AsyncIoUringSocket::ReadSqe::appendReadData(
    std::unique_ptr<IOBuf> data, std::unique_ptr<IOBuf>& overflow) noexcept {
  if (!data) {
    return;
  }

  if (overflow) {
    overflow->appendToChain(std::move(data));
  } else {
    overflow = std::move(data);
  }
}

void AsyncIoUringSocket::setPreReceivedData(std::unique_ptr<IOBuf> data) {
  readSqe_->appendPreReceive(std::move(data));
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

namespace {

struct DetachFdState : AsyncReader::ReadCallback {
  DetachFdState(
      AsyncIoUringSocket* s, AsyncDetachFdCallback* cb, NetworkSocket fd)
      : socket(s), callback(cb), ns(fd) {}
  AsyncIoUringSocket* socket;
  AsyncDetachFdCallback* callback;
  NetworkSocket ns;
  std::unique_ptr<IOBuf> unread;
  std::unique_ptr<IOBuf> buffer;

  void done() {
    socket->setReadCB(nullptr);
    callback->fdDetached(ns, std::move(unread));
    delete this;
  }

  // ReadCallback:
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    if (!buffer) {
      buffer = IOBuf::create(2000);
    }
    *bufReturn = buffer->writableTail();
    *lenReturn = buffer->tailroom();
  }

  void readErr(const AsyncSocketException&) noexcept override { done(); }
  void readEOF() noexcept override { done(); }
  void readBufferAvailable(std::unique_ptr<IOBuf> buf) noexcept override {
    if (unread) {
      unread->appendToChain(std::move(buf));
    } else {
      unread = std::move(buf);
    }
    if (!socket->readSqeInFlight()) {
      done();
    }
  }

  void readDataAvailable(size_t len) noexcept override {
    buffer->append(len);
    readBufferAvailable(std::move(buffer));
  }
  bool isBufferMovable() noexcept override { return true; }
};

struct CancelSqe : IoSqeBase {
  explicit CancelSqe(IoSqeBase* sqe) : target_(sqe) {}
  void processSubmit(struct io_uring_sqe* sqe) noexcept override {
    ::io_uring_prep_cancel(sqe, target_, 0);
  }
  void callback(int, uint32_t) noexcept override { delete this; }
  void callbackCancelled() noexcept override { delete this; }
  IoSqeBase* target_;
};

} // namespace

void AsyncIoUringSocket::asyncDetachFd(AsyncDetachFdCallback* callback) {
  auto state = new DetachFdState(this, callback, takeFd());

  if (writeSqeActive_) {
    backend_->cancel(writeSqeActive_);
    writeSqeActive_->callback_->writeErr(
        0, AsyncSocketException(AsyncSocketException::UNKNOWN, "fd detached"));
    writeSqeActive_ = nullptr;
  }
  while (!writeSqeQueue_.empty()) {
    auto& f = writeSqeQueue_.front();
    f.callback_->writeErr(
        0, AsyncSocketException(AsyncSocketException::UNKNOWN, "fd detached"));
    backend_->cancel(&f);
    writeSqeQueue_.pop_front();
  }

  setReadCB(state);
  if (readSqe_->inFlight()) {
    backend_->submitNow(*new CancelSqe(readSqe_.get()));
  } else {
    state->done();
  }

  // todo - care about connect? probably doesnt matter as we wont have bad
  // results (eg wrong read data), just a broken socket
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

NetworkSocket AsyncIoUringSocket::takeFd() {
  auto ret = std::exchange(fd_, {});
  if (fdRegistered_) {
    auto start = std::chrono::steady_clock::now();
    if (!backend_->unregisterFd(fdRegistered_)) {
      LOG(ERROR) << "Bad fd unregister";
    }
    auto end = std::chrono::steady_clock::now();
    if (end - start > std::chrono::milliseconds(1)) {
      LOG(INFO) << "unregistering fd took "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       end - start)
                       .count()
                << "us";
    }
  }
  fdRegistered_ = nullptr;
  usedFd_ = -1;
  return ret;
}

void AsyncIoUringSocket::closeProcessSubmit(struct io_uring_sqe* sqe) {
  if (fd_.toFd() >= 0) {
    ::io_uring_prep_close(sqe, fd_.toFd());
  } else {
    // already closed -> nop
    ::io_uring_prep_nop(sqe);
  }

  // the fd can be reused from this point
  takeFd();
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

  if (closeSqe_) {
    // todo: we should async close_direct registered fds and then not call
    // unregister on them

    // we submit and then release for 2 reasons:
    // 1: we dont want to accidentally clear the closeSqe_ without submitting
    // 2: we dont want to resubmit, which could close a random fd
    backend_->submitSoon(*closeSqe_);
    closeSqe_.release();
  }
  if (readSqe_) {
    ReadCallback* callback = readSqe_->readCallback();
    readSqe_->setReadCallback(nullptr);
    if (callback) {
      callback->readEOF();
    }
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

void AsyncIoUringSocket::cacheAddresses() {
  try {
    SocketAddress s;
    getLocalAddress(&s);
    getPeerAddress(&s);
  } catch (const std::system_error& e) {
    VLOG(2) << "Error caching addresses: " << e.code().value() << ", "
            << e.code().message();
  }
}

size_t AsyncIoUringSocket::getRawBytesReceived() const {
  return readSqe_->bytesReceived();
}

int AsyncIoUringSocket::setNoDelay(bool noDelay) {
  if (fd_ == NetworkSocket()) {
    VLOG(4) << "AsyncSocket::setNoDelay() called on non-open socket " << this;
    return EINVAL;
  }

  int value = noDelay ? 1 : 0;
  if (setSockOpt(IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) != 0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update TCP_NODELAY option on AsyncSocket " << this
            << " (fd=" << fd_ << "): " << errnoStr(errnoCopy);
    return errnoCopy;
  }
  return 0;
}

int AsyncIoUringSocket::setSockOpt(
    int level, int optname, const void* optval, socklen_t optsize) {
  return ::setsockopt(fd_.toFd(), level, optname, optval, optsize);
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

    // read does not use registered fd, as it can be long lived and leak socket
    // files
    readSqe_->setFd(fd_.toFd());
  } catch (std::exception const& e) {
    LOG(ERROR) << "unable to setFd " << ns.toFd() << " : " << e.what();
    ::close(ns.toFd());
    throw;
  }
}

} // namespace folly

#endif
