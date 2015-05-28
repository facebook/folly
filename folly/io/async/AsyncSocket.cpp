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

#include <folly/io/async/AsyncSocket.h>

#include <folly/io/async/EventBase.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>

#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

using std::string;
using std::unique_ptr;

namespace folly {

// static members initializers
const AsyncSocket::OptionMap AsyncSocket::emptyOptionMap;

const AsyncSocketException socketClosedLocallyEx(
    AsyncSocketException::END_OF_FILE, "socket closed locally");
const AsyncSocketException socketShutdownForWritesEx(
    AsyncSocketException::END_OF_FILE, "socket shutdown for writes");

// TODO: It might help performance to provide a version of WriteRequest that
// users could derive from, so we can avoid the extra allocation for each call
// to write()/writev().  We could templatize TFramedAsyncChannel just like the
// protocols are currently templatized for transports.
//
// We would need the version for external users where they provide the iovec
// storage space, and only our internal version would allocate it at the end of
// the WriteRequest.

/**
 * A WriteRequest object tracks information about a pending write operation.
 */
class AsyncSocket::WriteRequest {
 public:
  WriteRequest(AsyncSocket* socket,
               WriteRequest* next,
               WriteCallback* callback,
               uint32_t totalBytesWritten) :
    socket_(socket), next_(next), callback_(callback),
    totalBytesWritten_(totalBytesWritten) {}

  virtual void destroy() = 0;

  virtual bool performWrite() = 0;

  virtual void consume() = 0;

  virtual bool isComplete() = 0;

  WriteRequest* getNext() const {
    return next_;
  }

  WriteCallback* getCallback() const {
    return callback_;
  }

  uint32_t getTotalBytesWritten() const {
    return totalBytesWritten_;
  }

  void append(WriteRequest* next) {
    assert(next_ == nullptr);
    next_ = next;
  }

 protected:
  // protected destructor, to ensure callers use destroy()
  virtual ~WriteRequest() = default;

  AsyncSocket* socket_;         ///< parent socket
  WriteRequest* next_;          ///< pointer to next WriteRequest
  WriteCallback* callback_;     ///< completion callback
  uint32_t totalBytesWritten_;  ///< total bytes written
};

/* The default WriteRequest implementation, used for write(), writev() and
 * writeChain()
 *
 * A new BytesWriteRequest operation is allocated on the heap for all write
 * operations that cannot be completed immediately.
 */
class AsyncSocket::BytesWriteRequest : public AsyncSocket::WriteRequest {
 public:
  static BytesWriteRequest* newRequest(AsyncSocket* socket,
                                       WriteCallback* callback,
                                       const iovec* ops,
                                       uint32_t opCount,
                                       uint32_t partialWritten,
                                       uint32_t bytesWritten,
                                       unique_ptr<IOBuf>&& ioBuf,
                                       WriteFlags flags) {
    assert(opCount > 0);
    // Since we put a variable size iovec array at the end
    // of each BytesWriteRequest, we have to manually allocate the memory.
    void* buf = malloc(sizeof(BytesWriteRequest) +
                       (opCount * sizeof(struct iovec)));
    if (buf == nullptr) {
      throw std::bad_alloc();
    }

    return new(buf) BytesWriteRequest(socket, callback, ops, opCount,
                                      partialWritten, bytesWritten,
                                      std::move(ioBuf), flags);
  }

  void destroy() override {
    this->~BytesWriteRequest();
    free(this);
  }

  bool performWrite() override {
    WriteFlags writeFlags = flags_;
    if (getNext() != nullptr) {
      writeFlags = writeFlags | WriteFlags::CORK;
    }
    bytesWritten_ = socket_->performWrite(getOps(), getOpCount(), writeFlags,
                                          &opsWritten_, &partialBytes_);
    return bytesWritten_ >= 0;
  }

  bool isComplete() override {
    return opsWritten_ == getOpCount();
  }

  void consume() override {
    // Advance opIndex_ forward by opsWritten_
    opIndex_ += opsWritten_;
    assert(opIndex_ < opCount_);

    // If we've finished writing any IOBufs, release them
    if (ioBuf_) {
      for (uint32_t i = opsWritten_; i != 0; --i) {
        assert(ioBuf_);
        ioBuf_ = ioBuf_->pop();
      }
    }

    // Move partialBytes_ forward into the current iovec buffer
    struct iovec* currentOp = writeOps_ + opIndex_;
    assert((partialBytes_ < currentOp->iov_len) || (currentOp->iov_len == 0));
    currentOp->iov_base =
      reinterpret_cast<uint8_t*>(currentOp->iov_base) + partialBytes_;
    currentOp->iov_len -= partialBytes_;

    // Increment the totalBytesWritten_ count by bytesWritten_;
    totalBytesWritten_ += bytesWritten_;
  }

 private:
  BytesWriteRequest(AsyncSocket* socket,
                    WriteCallback* callback,
                    const struct iovec* ops,
                    uint32_t opCount,
                    uint32_t partialBytes,
                    uint32_t bytesWritten,
                    unique_ptr<IOBuf>&& ioBuf,
                    WriteFlags flags)
    : AsyncSocket::WriteRequest(socket, nullptr, callback, 0)
    , opCount_(opCount)
    , opIndex_(0)
    , flags_(flags)
    , ioBuf_(std::move(ioBuf))
    , opsWritten_(0)
    , partialBytes_(partialBytes)
    , bytesWritten_(bytesWritten) {
    memcpy(writeOps_, ops, sizeof(*ops) * opCount_);
  }

  // private destructor, to ensure callers use destroy()
  virtual ~BytesWriteRequest() = default;

  const struct iovec* getOps() const {
    assert(opCount_ > opIndex_);
    return writeOps_ + opIndex_;
  }

  uint32_t getOpCount() const {
    assert(opCount_ > opIndex_);
    return opCount_ - opIndex_;
  }

  uint32_t opCount_;            ///< number of entries in writeOps_
  uint32_t opIndex_;            ///< current index into writeOps_
  WriteFlags flags_;            ///< set for WriteFlags
  unique_ptr<IOBuf> ioBuf_;     ///< underlying IOBuf, or nullptr if N/A

  // for consume(), how much we wrote on the last write
  uint32_t opsWritten_;         ///< complete ops written
  uint32_t partialBytes_;       ///< partial bytes of incomplete op written
  ssize_t bytesWritten_;        ///< bytes written altogether

  struct iovec writeOps_[];     ///< write operation(s) list
};

AsyncSocket::AsyncSocket()
  : eventBase_(nullptr)
  , writeTimeout_(this, nullptr)
  , ioHandler_(this, nullptr) {
  VLOG(5) << "new AsyncSocket()";
  init();
}

AsyncSocket::AsyncSocket(EventBase* evb)
  : eventBase_(evb)
  , writeTimeout_(this, evb)
  , ioHandler_(this, evb) {
  VLOG(5) << "new AsyncSocket(" << this << ", evb=" << evb << ")";
  init();
}

AsyncSocket::AsyncSocket(EventBase* evb,
                           const folly::SocketAddress& address,
                           uint32_t connectTimeout)
  : AsyncSocket(evb) {
  connect(nullptr, address, connectTimeout);
}

AsyncSocket::AsyncSocket(EventBase* evb,
                           const std::string& ip,
                           uint16_t port,
                           uint32_t connectTimeout)
  : AsyncSocket(evb) {
  connect(nullptr, ip, port, connectTimeout);
}

AsyncSocket::AsyncSocket(EventBase* evb, int fd)
  : eventBase_(evb)
  , writeTimeout_(this, evb)
  , ioHandler_(this, evb, fd) {
  VLOG(5) << "new AsyncSocket(" << this << ", evb=" << evb << ", fd="
          << fd << ")";
  init();
  fd_ = fd;
  setCloseOnExec();
  state_ = StateEnum::ESTABLISHED;
}

// init() method, since constructor forwarding isn't supported in most
// compilers yet.
void AsyncSocket::init() {
  assert(eventBase_ == nullptr || eventBase_->isInEventBaseThread());
  shutdownFlags_ = 0;
  state_ = StateEnum::UNINIT;
  eventFlags_ = EventHandler::NONE;
  fd_ = -1;
  sendTimeout_ = 0;
  maxReadsPerEvent_ = 16;
  connectCallback_ = nullptr;
  readCallback_ = nullptr;
  writeReqHead_ = nullptr;
  writeReqTail_ = nullptr;
  shutdownSocketSet_ = nullptr;
  appBytesWritten_ = 0;
  appBytesReceived_ = 0;
}

AsyncSocket::~AsyncSocket() {
  VLOG(7) << "actual destruction of AsyncSocket(this=" << this
          << ", evb=" << eventBase_ << ", fd=" << fd_
          << ", state=" << state_ << ")";
}

void AsyncSocket::destroy() {
  VLOG(5) << "AsyncSocket::destroy(this=" << this << ", evb=" << eventBase_
          << ", fd=" << fd_ << ", state=" << state_;
  // When destroy is called, close the socket immediately
  closeNow();

  // Then call DelayedDestruction::destroy() to take care of
  // whether or not we need immediate or delayed destruction
  DelayedDestruction::destroy();
}

int AsyncSocket::detachFd() {
  VLOG(6) << "AsyncSocket::detachFd(this=" << this << ", fd=" << fd_
          << ", evb=" << eventBase_ << ", state=" << state_
          << ", events=" << std::hex << eventFlags_ << ")";
  // Extract the fd, and set fd_ to -1 first, so closeNow() won't
  // actually close the descriptor.
  if (shutdownSocketSet_) {
    shutdownSocketSet_->remove(fd_);
  }
  int fd = fd_;
  fd_ = -1;
  // Call closeNow() to invoke all pending callbacks with an error.
  closeNow();
  // Update the EventHandler to stop using this fd.
  // This can only be done after closeNow() unregisters the handler.
  ioHandler_.changeHandlerFD(-1);
  return fd;
}

const folly::SocketAddress& AsyncSocket::anyAddress() {
  static const folly::SocketAddress anyAddress =
    folly::SocketAddress("0.0.0.0", 0);
  return anyAddress;
}

void AsyncSocket::setShutdownSocketSet(ShutdownSocketSet* newSS) {
  if (shutdownSocketSet_ == newSS) {
    return;
  }
  if (shutdownSocketSet_ && fd_ != -1) {
    shutdownSocketSet_->remove(fd_);
  }
  shutdownSocketSet_ = newSS;
  if (shutdownSocketSet_ && fd_ != -1) {
    shutdownSocketSet_->add(fd_);
  }
}

void AsyncSocket::setCloseOnExec() {
  int rv = fcntl(fd_, F_SETFD, FD_CLOEXEC);
  if (rv != 0) {
    throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                               withAddr("failed to set close-on-exec flag"),
                               errno);
  }
}

void AsyncSocket::connect(ConnectCallback* callback,
                           const folly::SocketAddress& address,
                           int timeout,
                           const OptionMap &options,
                           const folly::SocketAddress& bindAddr) noexcept {
  DestructorGuard dg(this);
  assert(eventBase_->isInEventBaseThread());

  addr_ = address;

  // Make sure we're in the uninitialized state
  if (state_ != StateEnum::UNINIT) {
    return invalidState(callback);
  }

  assert(fd_ == -1);
  state_ = StateEnum::CONNECTING;
  connectCallback_ = callback;

  sockaddr_storage addrStorage;
  sockaddr* saddr = reinterpret_cast<sockaddr*>(&addrStorage);

  try {
    // Create the socket
    // Technically the first parameter should actually be a protocol family
    // constant (PF_xxx) rather than an address family (AF_xxx), but the
    // distinction is mainly just historical.  In pretty much all
    // implementations the PF_foo and AF_foo constants are identical.
    fd_ = socket(address.getFamily(), SOCK_STREAM, 0);
    if (fd_ < 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                                withAddr("failed to create socket"), errno);
    }
    if (shutdownSocketSet_) {
      shutdownSocketSet_->add(fd_);
    }
    ioHandler_.changeHandlerFD(fd_);

    setCloseOnExec();

    // Put the socket in non-blocking mode
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags == -1) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                                withAddr("failed to get socket flags"), errno);
    }
    int rv = fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    if (rv == -1) {
      throw AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          withAddr("failed to put socket in non-blocking mode"),
          errno);
    }

#if !defined(MSG_NOSIGNAL) && defined(F_SETNOSIGPIPE)
    // iOS and OS X don't support MSG_NOSIGNAL; set F_SETNOSIGPIPE instead
    rv = fcntl(fd_, F_SETNOSIGPIPE, 1);
    if (rv == -1) {
      throw AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "failed to enable F_SETNOSIGPIPE on socket",
          errno);
    }
#endif

    // By default, turn on TCP_NODELAY
    // If setNoDelay() fails, we continue anyway; this isn't a fatal error.
    // setNoDelay() will log an error message if it fails.
    if (address.getFamily() != AF_UNIX) {
      (void)setNoDelay(true);
    }

    VLOG(5) << "AsyncSocket::connect(this=" << this << ", evb=" << eventBase_
            << ", fd=" << fd_ << ", host=" << address.describe().c_str();

    // bind the socket
    if (bindAddr != anyAddress()) {
      int one = 1;
      if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
        doClose();
        throw AsyncSocketException(
          AsyncSocketException::NOT_OPEN,
          "failed to setsockopt prior to bind on " + bindAddr.describe(),
          errno);
      }

      bindAddr.getAddress(&addrStorage);

      if (::bind(fd_, saddr, bindAddr.getActualSize()) != 0) {
        doClose();
        throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
                                  "failed to bind to async socket: " +
                                  bindAddr.describe(),
                                  errno);
      }
    }

    // Apply the additional options if any.
    for (const auto& opt: options) {
      int rv = opt.first.apply(fd_, opt.second);
      if (rv != 0) {
        throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                                  withAddr("failed to set socket option"),
                                  errno);
      }
    }

    // Perform the connect()
    address.getAddress(&addrStorage);

    rv = ::connect(fd_, saddr, address.getActualSize());
    if (rv < 0) {
      if (errno == EINPROGRESS) {
        // Connection in progress.
        if (timeout > 0) {
          // Start a timer in case the connection takes too long.
          if (!writeTimeout_.scheduleTimeout(timeout)) {
            throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                withAddr("failed to schedule AsyncSocket connect timeout"));
          }
        }

        // Register for write events, so we'll
        // be notified when the connection finishes/fails.
        // Note that we don't register for a persistent event here.
        assert(eventFlags_ == EventHandler::NONE);
        eventFlags_ = EventHandler::WRITE;
        if (!ioHandler_.registerHandler(eventFlags_)) {
          throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
              withAddr("failed to register AsyncSocket connect handler"));
        }
        return;
      } else {
        throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
                                  "connect failed (immediately)", errno);
      }
    }

    // If we're still here the connect() succeeded immediately.
    // Fall through to call the callback outside of this try...catch block
  } catch (const AsyncSocketException& ex) {
    return failConnect(__func__, ex);
  } catch (const std::exception& ex) {
    // shouldn't happen, but handle it just in case
    VLOG(4) << "AsyncSocket::connect(this=" << this << ", fd=" << fd_
               << "): unexpected " << typeid(ex).name() << " exception: "
               << ex.what();
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
                            withAddr(string("unexpected exception: ") +
                                     ex.what()));
    return failConnect(__func__, tex);
  }

  // The connection succeeded immediately
  // The read callback may not have been set yet, and no writes may be pending
  // yet, so we don't have to register for any events at the moment.
  VLOG(8) << "AsyncSocket::connect succeeded immediately; this=" << this;
  assert(readCallback_ == nullptr);
  assert(writeReqHead_ == nullptr);
  state_ = StateEnum::ESTABLISHED;
  if (callback) {
    connectCallback_ = nullptr;
    callback->connectSuccess();
  }
}

void AsyncSocket::connect(ConnectCallback* callback,
                           const string& ip, uint16_t port,
                           int timeout,
                           const OptionMap &options) noexcept {
  DestructorGuard dg(this);
  try {
    connectCallback_ = callback;
    connect(callback, folly::SocketAddress(ip, port), timeout, options);
  } catch (const std::exception& ex) {
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
                            ex.what());
    return failConnect(__func__, tex);
  }
}

void AsyncSocket::cancelConnect() {
  connectCallback_ = nullptr;
  if (state_ == StateEnum::CONNECTING) {
    closeNow();
  }
}

void AsyncSocket::setSendTimeout(uint32_t milliseconds) {
  sendTimeout_ = milliseconds;
  assert(eventBase_ == nullptr || eventBase_->isInEventBaseThread());

  // If we are currently pending on write requests, immediately update
  // writeTimeout_ with the new value.
  if ((eventFlags_ & EventHandler::WRITE) &&
      (state_ != StateEnum::CONNECTING)) {
    assert(state_ == StateEnum::ESTABLISHED);
    assert((shutdownFlags_ & SHUT_WRITE) == 0);
    if (sendTimeout_ > 0) {
      if (!writeTimeout_.scheduleTimeout(sendTimeout_)) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
            withAddr("failed to reschedule send timeout in setSendTimeout"));
        return failWrite(__func__, ex);
      }
    } else {
      writeTimeout_.cancelTimeout();
    }
  }
}

void AsyncSocket::setReadCB(ReadCallback *callback) {
  VLOG(6) << "AsyncSocket::setReadCallback() this=" << this << ", fd=" << fd_
          << ", callback=" << callback << ", state=" << state_;

  // Short circuit if callback is the same as the existing readCallback_.
  //
  // Note that this is needed for proper functioning during some cleanup cases.
  // During cleanup we allow setReadCallback(nullptr) to be called even if the
  // read callback is already unset and we have been detached from an event
  // base.  This check prevents us from asserting
  // eventBase_->isInEventBaseThread() when eventBase_ is nullptr.
  if (callback == readCallback_) {
    return;
  }

  if (shutdownFlags_ & SHUT_READ) {
    // Reads have already been shut down on this socket.
    //
    // Allow setReadCallback(nullptr) to be called in this case, but don't
    // allow a new callback to be set.
    //
    // For example, setReadCallback(nullptr) can happen after an error if we
    // invoke some other error callback before invoking readError().  The other
    // error callback that is invoked first may go ahead and clear the read
    // callback before we get a chance to invoke readError().
    if (callback != nullptr) {
      return invalidState(callback);
    }
    assert((eventFlags_ & EventHandler::READ) == 0);
    readCallback_ = nullptr;
    return;
  }

  DestructorGuard dg(this);
  assert(eventBase_->isInEventBaseThread());

  switch ((StateEnum)state_) {
    case StateEnum::CONNECTING:
      // For convenience, we allow the read callback to be set while we are
      // still connecting.  We just store the callback for now.  Once the
      // connection completes we'll register for read events.
      readCallback_ = callback;
      return;
    case StateEnum::ESTABLISHED:
    {
      readCallback_ = callback;
      uint16_t oldFlags = eventFlags_;
      if (readCallback_) {
        eventFlags_ |= EventHandler::READ;
      } else {
        eventFlags_ &= ~EventHandler::READ;
      }

      // Update our registration if our flags have changed
      if (eventFlags_ != oldFlags) {
        // We intentionally ignore the return value here.
        // updateEventRegistration() will move us into the error state if it
        // fails, and we don't need to do anything else here afterwards.
        (void)updateEventRegistration();
      }

      if (readCallback_) {
        checkForImmediateRead();
      }
      return;
    }
    case StateEnum::CLOSED:
    case StateEnum::ERROR:
      // We should never reach here.  SHUT_READ should always be set
      // if we are in STATE_CLOSED or STATE_ERROR.
      assert(false);
      return invalidState(callback);
    case StateEnum::UNINIT:
      // We do not allow setReadCallback() to be called before we start
      // connecting.
      return invalidState(callback);
  }

  // We don't put a default case in the switch statement, so that the compiler
  // will warn us to update the switch statement if a new state is added.
  return invalidState(callback);
}

AsyncSocket::ReadCallback* AsyncSocket::getReadCallback() const {
  return readCallback_;
}

void AsyncSocket::write(WriteCallback* callback,
                         const void* buf, size_t bytes, WriteFlags flags) {
  iovec op;
  op.iov_base = const_cast<void*>(buf);
  op.iov_len = bytes;
  writeImpl(callback, &op, 1, std::move(unique_ptr<IOBuf>()), flags);
}

void AsyncSocket::writev(WriteCallback* callback,
                          const iovec* vec,
                          size_t count,
                          WriteFlags flags) {
  writeImpl(callback, vec, count, std::move(unique_ptr<IOBuf>()), flags);
}

void AsyncSocket::writeChain(WriteCallback* callback, unique_ptr<IOBuf>&& buf,
                              WriteFlags flags) {
  size_t count = buf->countChainElements();
  if (count <= 64) {
    iovec vec[count];
    writeChainImpl(callback, vec, count, std::move(buf), flags);
  } else {
    iovec* vec = new iovec[count];
    writeChainImpl(callback, vec, count, std::move(buf), flags);
    delete[] vec;
  }
}

void AsyncSocket::writeChainImpl(WriteCallback* callback, iovec* vec,
    size_t count, unique_ptr<IOBuf>&& buf, WriteFlags flags) {
  size_t veclen = buf->fillIov(vec, count);
  writeImpl(callback, vec, veclen, std::move(buf), flags);
}

void AsyncSocket::writeImpl(WriteCallback* callback, const iovec* vec,
                             size_t count, unique_ptr<IOBuf>&& buf,
                             WriteFlags flags) {
  VLOG(6) << "AsyncSocket::writev() this=" << this << ", fd=" << fd_
          << ", callback=" << callback << ", count=" << count
          << ", state=" << state_;
  DestructorGuard dg(this);
  unique_ptr<IOBuf>ioBuf(std::move(buf));
  assert(eventBase_->isInEventBaseThread());

  if (shutdownFlags_ & (SHUT_WRITE | SHUT_WRITE_PENDING)) {
    // No new writes may be performed after the write side of the socket has
    // been shutdown.
    //
    // We could just call callback->writeError() here to fail just this write.
    // However, fail hard and use invalidState() to fail all outstanding
    // callbacks and move the socket into the error state.  There's most likely
    // a bug in the caller's code, so we abort everything rather than trying to
    // proceed as best we can.
    return invalidState(callback);
  }

  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  int bytesWritten = 0;
  bool mustRegister = false;
  if (state_ == StateEnum::ESTABLISHED && !connecting()) {
    if (writeReqHead_ == nullptr) {
      // If we are established and there are no other writes pending,
      // we can attempt to perform the write immediately.
      assert(writeReqTail_ == nullptr);
      assert((eventFlags_ & EventHandler::WRITE) == 0);

      bytesWritten = performWrite(vec, count, flags,
                                  &countWritten, &partialWritten);
      if (bytesWritten < 0) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                               withAddr("writev failed"), errno);
        return failWrite(__func__, callback, 0, ex);
      } else if (countWritten == count) {
        // We successfully wrote everything.
        // Invoke the callback and return.
        if (callback) {
          callback->writeSuccess();
        }
        return;
      } // else { continue writing the next writeReq }
      mustRegister = true;
    }
  } else if (!connecting()) {
    // Invalid state for writing
    return invalidState(callback);
  }

  // Create a new WriteRequest to add to the queue
  WriteRequest* req;
  try {
    req = BytesWriteRequest::newRequest(this, callback, vec + countWritten,
                                        count - countWritten, partialWritten,
                                        bytesWritten, std::move(ioBuf), flags);
  } catch (const std::exception& ex) {
    // we mainly expect to catch std::bad_alloc here
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
        withAddr(string("failed to append new WriteRequest: ") + ex.what()));
    return failWrite(__func__, callback, bytesWritten, tex);
  }
  req->consume();
  if (writeReqTail_ == nullptr) {
    assert(writeReqHead_ == nullptr);
    writeReqHead_ = writeReqTail_ = req;
  } else {
    writeReqTail_->append(req);
    writeReqTail_ = req;
  }

  // Register for write events if are established and not currently
  // waiting on write events
  if (mustRegister) {
    assert(state_ == StateEnum::ESTABLISHED);
    assert((eventFlags_ & EventHandler::WRITE) == 0);
    if (!updateEventRegistration(EventHandler::WRITE, 0)) {
      assert(state_ == StateEnum::ERROR);
      return;
    }
    if (sendTimeout_ > 0) {
      // Schedule a timeout to fire if the write takes too long.
      if (!writeTimeout_.scheduleTimeout(sendTimeout_)) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                               withAddr("failed to schedule send timeout"));
        return failWrite(__func__, ex);
      }
    }
  }
}

void AsyncSocket::close() {
  VLOG(5) << "AsyncSocket::close(): this=" << this << ", fd_=" << fd_
          << ", state=" << state_ << ", shutdownFlags="
          << std::hex << (int) shutdownFlags_;

  // close() is only different from closeNow() when there are pending writes
  // that need to drain before we can close.  In all other cases, just call
  // closeNow().
  //
  // Note that writeReqHead_ can be non-nullptr even in STATE_CLOSED or
  // STATE_ERROR if close() is invoked while a previous closeNow() or failure
  // is still running.  (e.g., If there are multiple pending writes, and we
  // call writeError() on the first one, it may call close().  In this case we
  // will already be in STATE_CLOSED or STATE_ERROR, but the remaining pending
  // writes will still be in the queue.)
  //
  // We only need to drain pending writes if we are still in STATE_CONNECTING
  // or STATE_ESTABLISHED
  if ((writeReqHead_ == nullptr) ||
      !(state_ == StateEnum::CONNECTING ||
      state_ == StateEnum::ESTABLISHED)) {
    closeNow();
    return;
  }

  // Declare a DestructorGuard to ensure that the AsyncSocket cannot be
  // destroyed until close() returns.
  DestructorGuard dg(this);
  assert(eventBase_->isInEventBaseThread());

  // Since there are write requests pending, we have to set the
  // SHUT_WRITE_PENDING flag, and wait to perform the real close until the
  // connect finishes and we finish writing these requests.
  //
  // Set SHUT_READ to indicate that reads are shut down, and set the
  // SHUT_WRITE_PENDING flag to mark that we want to shutdown once the
  // pending writes complete.
  shutdownFlags_ |= (SHUT_READ | SHUT_WRITE_PENDING);

  // If a read callback is set, invoke readEOF() immediately to inform it that
  // the socket has been closed and no more data can be read.
  if (readCallback_) {
    // Disable reads if they are enabled
    if (!updateEventRegistration(0, EventHandler::READ)) {
      // We're now in the error state; callbacks have been cleaned up
      assert(state_ == StateEnum::ERROR);
      assert(readCallback_ == nullptr);
    } else {
      ReadCallback* callback = readCallback_;
      readCallback_ = nullptr;
      callback->readEOF();
    }
  }
}

void AsyncSocket::closeNow() {
  VLOG(5) << "AsyncSocket::closeNow(): this=" << this << ", fd_=" << fd_
          << ", state=" << state_ << ", shutdownFlags="
          << std::hex << (int) shutdownFlags_;
  DestructorGuard dg(this);
  assert(eventBase_ == nullptr || eventBase_->isInEventBaseThread());

  switch (state_) {
    case StateEnum::ESTABLISHED:
    case StateEnum::CONNECTING:
    {
      shutdownFlags_ |= (SHUT_READ | SHUT_WRITE);
      state_ = StateEnum::CLOSED;

      // If the write timeout was set, cancel it.
      writeTimeout_.cancelTimeout();

      // If we are registered for I/O events, unregister.
      if (eventFlags_ != EventHandler::NONE) {
        eventFlags_ = EventHandler::NONE;
        if (!updateEventRegistration()) {
          // We will have been moved into the error state.
          assert(state_ == StateEnum::ERROR);
          return;
        }
      }

      if (fd_ >= 0) {
        ioHandler_.changeHandlerFD(-1);
        doClose();
      }

      if (connectCallback_) {
        ConnectCallback* callback = connectCallback_;
        connectCallback_ = nullptr;
        callback->connectErr(socketClosedLocallyEx);
      }

      failAllWrites(socketClosedLocallyEx);

      if (readCallback_) {
        ReadCallback* callback = readCallback_;
        readCallback_ = nullptr;
        callback->readEOF();
      }
      return;
    }
    case StateEnum::CLOSED:
      // Do nothing.  It's possible that we are being called recursively
      // from inside a callback that we invoked inside another call to close()
      // that is still running.
      return;
    case StateEnum::ERROR:
      // Do nothing.  The error handling code has performed (or is performing)
      // cleanup.
      return;
    case StateEnum::UNINIT:
      assert(eventFlags_ == EventHandler::NONE);
      assert(connectCallback_ == nullptr);
      assert(readCallback_ == nullptr);
      assert(writeReqHead_ == nullptr);
      shutdownFlags_ |= (SHUT_READ | SHUT_WRITE);
      state_ = StateEnum::CLOSED;
      return;
  }

  LOG(DFATAL) << "AsyncSocket::closeNow() (this=" << this << ", fd=" << fd_
              << ") called in unknown state " << state_;
}

void AsyncSocket::closeWithReset() {
  // Enable SO_LINGER, with the linger timeout set to 0.
  // This will trigger a TCP reset when we close the socket.
  if (fd_ >= 0) {
    struct linger optLinger = {1, 0};
    if (setSockOpt(SOL_SOCKET, SO_LINGER, &optLinger) != 0) {
      VLOG(2) << "AsyncSocket::closeWithReset(): error setting SO_LINGER "
              << "on " << fd_ << ": errno=" << errno;
    }
  }

  // Then let closeNow() take care of the rest
  closeNow();
}

void AsyncSocket::shutdownWrite() {
  VLOG(5) << "AsyncSocket::shutdownWrite(): this=" << this << ", fd=" << fd_
          << ", state=" << state_ << ", shutdownFlags="
          << std::hex << (int) shutdownFlags_;

  // If there are no pending writes, shutdownWrite() is identical to
  // shutdownWriteNow().
  if (writeReqHead_ == nullptr) {
    shutdownWriteNow();
    return;
  }

  assert(eventBase_->isInEventBaseThread());

  // There are pending writes.  Set SHUT_WRITE_PENDING so that the actual
  // shutdown will be performed once all writes complete.
  shutdownFlags_ |= SHUT_WRITE_PENDING;
}

void AsyncSocket::shutdownWriteNow() {
  VLOG(5) << "AsyncSocket::shutdownWriteNow(): this=" << this
          << ", fd=" << fd_ << ", state=" << state_
          << ", shutdownFlags=" << std::hex << (int) shutdownFlags_;

  if (shutdownFlags_ & SHUT_WRITE) {
    // Writes are already shutdown; nothing else to do.
    return;
  }

  // If SHUT_READ is already set, just call closeNow() to completely
  // close the socket.  This can happen if close() was called with writes
  // pending, and then shutdownWriteNow() is called before all pending writes
  // complete.
  if (shutdownFlags_ & SHUT_READ) {
    closeNow();
    return;
  }

  DestructorGuard dg(this);
  assert(eventBase_ == nullptr || eventBase_->isInEventBaseThread());

  switch (static_cast<StateEnum>(state_)) {
    case StateEnum::ESTABLISHED:
    {
      shutdownFlags_ |= SHUT_WRITE;

      // If the write timeout was set, cancel it.
      writeTimeout_.cancelTimeout();

      // If we are registered for write events, unregister.
      if (!updateEventRegistration(0, EventHandler::WRITE)) {
        // We will have been moved into the error state.
        assert(state_ == StateEnum::ERROR);
        return;
      }

      // Shutdown writes on the file descriptor
      ::shutdown(fd_, SHUT_WR);

      // Immediately fail all write requests
      failAllWrites(socketShutdownForWritesEx);
      return;
    }
    case StateEnum::CONNECTING:
    {
      // Set the SHUT_WRITE_PENDING flag.
      // When the connection completes, it will check this flag,
      // shutdown the write half of the socket, and then set SHUT_WRITE.
      shutdownFlags_ |= SHUT_WRITE_PENDING;

      // Immediately fail all write requests
      failAllWrites(socketShutdownForWritesEx);
      return;
    }
    case StateEnum::UNINIT:
      // Callers normally shouldn't call shutdownWriteNow() before the socket
      // even starts connecting.  Nonetheless, go ahead and set
      // SHUT_WRITE_PENDING.  Once the socket eventually connects it will
      // immediately shut down the write side of the socket.
      shutdownFlags_ |= SHUT_WRITE_PENDING;
      return;
    case StateEnum::CLOSED:
    case StateEnum::ERROR:
      // We should never get here.  SHUT_WRITE should always be set
      // in STATE_CLOSED and STATE_ERROR.
      VLOG(4) << "AsyncSocket::shutdownWriteNow() (this=" << this
                 << ", fd=" << fd_ << ") in unexpected state " << state_
                 << " with SHUT_WRITE not set ("
                 << std::hex << (int) shutdownFlags_ << ")";
      assert(false);
      return;
  }

  LOG(DFATAL) << "AsyncSocket::shutdownWriteNow() (this=" << this << ", fd="
              << fd_ << ") called in unknown state " << state_;
}

bool AsyncSocket::readable() const {
  if (fd_ == -1) {
    return false;
  }
  struct pollfd fds[1];
  fds[0].fd = fd_;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  int rc = poll(fds, 1, 0);
  return rc == 1;
}

bool AsyncSocket::isPending() const {
  return ioHandler_.isPending();
}

bool AsyncSocket::hangup() const {
  if (fd_ == -1) {
    // sanity check, no one should ask for hangup if we are not connected.
    assert(false);
    return false;
  }
#ifdef POLLRDHUP // Linux-only
  struct pollfd fds[1];
  fds[0].fd = fd_;
  fds[0].events = POLLRDHUP|POLLHUP;
  fds[0].revents = 0;
  poll(fds, 1, 0);
  return (fds[0].revents & (POLLRDHUP|POLLHUP)) != 0;
#else
  return false;
#endif
}

bool AsyncSocket::good() const {
  return ((state_ == StateEnum::CONNECTING ||
          state_ == StateEnum::ESTABLISHED) &&
          (shutdownFlags_ == 0) && (eventBase_ != nullptr));
}

bool AsyncSocket::error() const {
  return (state_ == StateEnum::ERROR);
}

void AsyncSocket::attachEventBase(EventBase* eventBase) {
  VLOG(5) << "AsyncSocket::attachEventBase(this=" << this << ", fd=" << fd_
          << ", old evb=" << eventBase_ << ", new evb=" << eventBase
          << ", state=" << state_ << ", events="
          << std::hex << eventFlags_ << ")";
  assert(eventBase_ == nullptr);
  assert(eventBase->isInEventBaseThread());

  eventBase_ = eventBase;
  ioHandler_.attachEventBase(eventBase);
  writeTimeout_.attachEventBase(eventBase);
}

void AsyncSocket::detachEventBase() {
  VLOG(5) << "AsyncSocket::detachEventBase(this=" << this << ", fd=" << fd_
          << ", old evb=" << eventBase_ << ", state=" << state_
          << ", events=" << std::hex << eventFlags_ << ")";
  assert(eventBase_ != nullptr);
  assert(eventBase_->isInEventBaseThread());

  eventBase_ = nullptr;
  ioHandler_.detachEventBase();
  writeTimeout_.detachEventBase();
}

bool AsyncSocket::isDetachable() const {
  DCHECK(eventBase_ != nullptr);
  DCHECK(eventBase_->isInEventBaseThread());

  return !ioHandler_.isHandlerRegistered() && !writeTimeout_.isScheduled();
}

void AsyncSocket::getLocalAddress(folly::SocketAddress* address) const {
  address->setFromLocalAddress(fd_);
}

void AsyncSocket::getPeerAddress(folly::SocketAddress* address) const {
  if (!addr_.isInitialized()) {
    addr_.setFromPeerAddress(fd_);
  }
  *address = addr_;
}

int AsyncSocket::setNoDelay(bool noDelay) {
  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setNoDelay() called on non-open socket "
               << this << "(state=" << state_ << ")";
    return EINVAL;

  }

  int value = noDelay ? 1 : 0;
  if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) != 0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update TCP_NODELAY option on AsyncSocket "
            << this << " (fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
}

int AsyncSocket::setCongestionFlavor(const std::string &cname) {

  #ifndef TCP_CONGESTION
  #define TCP_CONGESTION  13
  #endif

  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setCongestionFlavor() called on non-open "
               << "socket " << this << "(state=" << state_ << ")";
    return EINVAL;

  }

  if (setsockopt(fd_, IPPROTO_TCP, TCP_CONGESTION, cname.c_str(),
        cname.length() + 1) != 0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update TCP_CONGESTION option on AsyncSocket "
            << this << "(fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
}

int AsyncSocket::setQuickAck(bool quickack) {
  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setQuickAck() called on non-open socket "
               << this << "(state=" << state_ << ")";
    return EINVAL;

  }

#ifdef TCP_QUICKACK // Linux-only
  int value = quickack ? 1 : 0;
  if (setsockopt(fd_, IPPROTO_TCP, TCP_QUICKACK, &value, sizeof(value)) != 0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update TCP_QUICKACK option on AsyncSocket"
            << this << "(fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
#else
  return ENOSYS;
#endif
}

int AsyncSocket::setSendBufSize(size_t bufsize) {
  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setSendBufSize() called on non-open socket "
               << this << "(state=" << state_ << ")";
    return EINVAL;
  }

  if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) !=0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update SO_SNDBUF option on AsyncSocket"
            << this << "(fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
}

int AsyncSocket::setRecvBufSize(size_t bufsize) {
  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setRecvBufSize() called on non-open socket "
               << this << "(state=" << state_ << ")";
    return EINVAL;
  }

  if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) !=0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to update SO_RCVBUF option on AsyncSocket"
            << this << "(fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
}

int AsyncSocket::setTCPProfile(int profd) {
  if (fd_ < 0) {
    VLOG(4) << "AsyncSocket::setTCPProfile() called on non-open socket "
               << this << "(state=" << state_ << ")";
    return EINVAL;
  }

  if (setsockopt(fd_, SOL_SOCKET, SO_SET_NAMESPACE, &profd, sizeof(int)) !=0) {
    int errnoCopy = errno;
    VLOG(2) << "failed to set socket namespace option on AsyncSocket"
            << this << "(fd=" << fd_ << ", state=" << state_ << "): "
            << strerror(errnoCopy);
    return errnoCopy;
  }

  return 0;
}

void AsyncSocket::ioReady(uint16_t events) noexcept {
  VLOG(7) << "AsyncSocket::ioRead() this=" << this << ", fd" << fd_
          << ", events=" << std::hex << events << ", state=" << state_;
  DestructorGuard dg(this);
  assert(events & EventHandler::READ_WRITE);
  assert(eventBase_->isInEventBaseThread());

  uint16_t relevantEvents = events & EventHandler::READ_WRITE;
  if (relevantEvents == EventHandler::READ) {
    handleRead();
  } else if (relevantEvents == EventHandler::WRITE) {
    handleWrite();
  } else if (relevantEvents == EventHandler::READ_WRITE) {
    EventBase* originalEventBase = eventBase_;
    // If both read and write events are ready, process writes first.
    handleWrite();

    // Return now if handleWrite() detached us from our EventBase
    if (eventBase_ != originalEventBase) {
      return;
    }

    // Only call handleRead() if a read callback is still installed.
    // (It's possible that the read callback was uninstalled during
    // handleWrite().)
    if (readCallback_) {
      handleRead();
    }
  } else {
    VLOG(4) << "AsyncSocket::ioRead() called with unexpected events "
               << std::hex << events << "(this=" << this << ")";
    abort();
  }
}

ssize_t AsyncSocket::performRead(void* buf, size_t buflen) {
  ssize_t bytes = recv(fd_, buf, buflen, MSG_DONTWAIT);
  if (bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // No more data to read right now.
      return READ_BLOCKING;
    } else {
      return READ_ERROR;
    }
  } else {
    appBytesReceived_ += bytes;
    return bytes;
  }
}

void AsyncSocket::handleRead() noexcept {
  VLOG(5) << "AsyncSocket::handleRead() this=" << this << ", fd=" << fd_
          << ", state=" << state_;
  assert(state_ == StateEnum::ESTABLISHED);
  assert((shutdownFlags_ & SHUT_READ) == 0);
  assert(readCallback_ != nullptr);
  assert(eventFlags_ & EventHandler::READ);

  // Loop until:
  // - a read attempt would block
  // - readCallback_ is uninstalled
  // - the number of loop iterations exceeds the optional maximum
  // - this AsyncSocket is moved to another EventBase
  //
  // When we invoke readDataAvailable() it may uninstall the readCallback_,
  // which is why need to check for it here.
  //
  // The last bullet point is slightly subtle.  readDataAvailable() may also
  // detach this socket from this EventBase.  However, before
  // readDataAvailable() returns another thread may pick it up, attach it to
  // a different EventBase, and install another readCallback_.  We need to
  // exit immediately after readDataAvailable() returns if the eventBase_ has
  // changed.  (The caller must perform some sort of locking to transfer the
  // AsyncSocket between threads properly.  This will be sufficient to ensure
  // that this thread sees the updated eventBase_ variable after
  // readDataAvailable() returns.)
  uint16_t numReads = 0;
  EventBase* originalEventBase = eventBase_;
  while (readCallback_ && eventBase_ == originalEventBase) {
    // Get the buffer to read into.
    void* buf = nullptr;
    size_t buflen = 0;
    try {
      readCallback_->getReadBuffer(&buf, &buflen);
    } catch (const AsyncSocketException& ex) {
      return failRead(__func__, ex);
    } catch (const std::exception& ex) {
      AsyncSocketException tex(AsyncSocketException::BAD_ARGS,
                              string("ReadCallback::getReadBuffer() "
                                     "threw exception: ") +
                              ex.what());
      return failRead(__func__, tex);
    } catch (...) {
      AsyncSocketException ex(AsyncSocketException::BAD_ARGS,
                             "ReadCallback::getReadBuffer() threw "
                             "non-exception type");
      return failRead(__func__, ex);
    }
    if (buf == nullptr || buflen == 0) {
      AsyncSocketException ex(AsyncSocketException::BAD_ARGS,
                             "ReadCallback::getReadBuffer() returned "
                             "empty buffer");
      return failRead(__func__, ex);
    }

    // Perform the read
    ssize_t bytesRead = performRead(buf, buflen);
    if (bytesRead > 0) {
      readCallback_->readDataAvailable(bytesRead);
      // Fall through and continue around the loop if the read
      // completely filled the available buffer.
      // Note that readCallback_ may have been uninstalled or changed inside
      // readDataAvailable().
      if (size_t(bytesRead) < buflen) {
        return;
      }
    } else if (bytesRead == READ_BLOCKING) {
        // No more data to read right now.
        return;
    } else if (bytesRead == READ_ERROR) {
      AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                             withAddr("recv() failed"), errno);
      return failRead(__func__, ex);
    } else {
      assert(bytesRead == READ_EOF);
      // EOF
      shutdownFlags_ |= SHUT_READ;
      if (!updateEventRegistration(0, EventHandler::READ)) {
        // we've already been moved into STATE_ERROR
        assert(state_ == StateEnum::ERROR);
        assert(readCallback_ == nullptr);
        return;
      }

      ReadCallback* callback = readCallback_;
      readCallback_ = nullptr;
      callback->readEOF();
      return;
    }
    if (maxReadsPerEvent_ && (++numReads >= maxReadsPerEvent_)) {
      return;
    }
  }
}

/**
 * This function attempts to write as much data as possible, until no more data
 * can be written.
 *
 * - If it sends all available data, it unregisters for write events, and stops
 *   the writeTimeout_.
 *
 * - If not all of the data can be sent immediately, it reschedules
 *   writeTimeout_ (if a non-zero timeout is set), and ensures the handler is
 *   registered for write events.
 */
void AsyncSocket::handleWrite() noexcept {
  VLOG(5) << "AsyncSocket::handleWrite() this=" << this << ", fd=" << fd_
          << ", state=" << state_;
  if (state_ == StateEnum::CONNECTING) {
    handleConnect();
    return;
  }

  // Normal write
  assert(state_ == StateEnum::ESTABLISHED);
  assert((shutdownFlags_ & SHUT_WRITE) == 0);
  assert(writeReqHead_ != nullptr);

  // Loop until we run out of write requests,
  // or until this socket is moved to another EventBase.
  // (See the comment in handleRead() explaining how this can happen.)
  EventBase* originalEventBase = eventBase_;
  while (writeReqHead_ != nullptr && eventBase_ == originalEventBase) {
    if (!writeReqHead_->performWrite()) {
      AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                             withAddr("writev() failed"), errno);
      return failWrite(__func__, ex);
    } else if (writeReqHead_->isComplete()) {
      // We finished this request
      WriteRequest* req = writeReqHead_;
      writeReqHead_ = req->getNext();

      if (writeReqHead_ == nullptr) {
        writeReqTail_ = nullptr;
        // This is the last write request.
        // Unregister for write events and cancel the send timer
        // before we invoke the callback.  We have to update the state properly
        // before calling the callback, since it may want to detach us from
        // the EventBase.
        if (eventFlags_ & EventHandler::WRITE) {
          if (!updateEventRegistration(0, EventHandler::WRITE)) {
            assert(state_ == StateEnum::ERROR);
            return;
          }
          // Stop the send timeout
          writeTimeout_.cancelTimeout();
        }
        assert(!writeTimeout_.isScheduled());

        // If SHUT_WRITE_PENDING is set, we should shutdown the socket after
        // we finish sending the last write request.
        //
        // We have to do this before invoking writeSuccess(), since
        // writeSuccess() may detach us from our EventBase.
        if (shutdownFlags_ & SHUT_WRITE_PENDING) {
          assert(connectCallback_ == nullptr);
          shutdownFlags_ |= SHUT_WRITE;

          if (shutdownFlags_ & SHUT_READ) {
            // Reads have already been shutdown.  Fully close the socket and
            // move to STATE_CLOSED.
            //
            // Note: This code currently moves us to STATE_CLOSED even if
            // close() hasn't ever been called.  This can occur if we have
            // received EOF from the peer and shutdownWrite() has been called
            // locally.  Should we bother staying in STATE_ESTABLISHED in this
            // case, until close() is actually called?  I can't think of a
            // reason why we would need to do so.  No other operations besides
            // calling close() or destroying the socket can be performed at
            // this point.
            assert(readCallback_ == nullptr);
            state_ = StateEnum::CLOSED;
            if (fd_ >= 0) {
              ioHandler_.changeHandlerFD(-1);
              doClose();
            }
          } else {
            // Reads are still enabled, so we are only doing a half-shutdown
            ::shutdown(fd_, SHUT_WR);
          }
        }
      }

      // Invoke the callback
      WriteCallback* callback = req->getCallback();
      req->destroy();
      if (callback) {
        callback->writeSuccess();
      }
      // We'll continue around the loop, trying to write another request
    } else {
      // Partial write.
      writeReqHead_->consume();
      // Stop after a partial write; it's highly likely that a subsequent write
      // attempt will just return EAGAIN.
      //
      // Ensure that we are registered for write events.
      if ((eventFlags_ & EventHandler::WRITE) == 0) {
        if (!updateEventRegistration(EventHandler::WRITE, 0)) {
          assert(state_ == StateEnum::ERROR);
          return;
        }
      }

      // Reschedule the send timeout, since we have made some write progress.
      if (sendTimeout_ > 0) {
        if (!writeTimeout_.scheduleTimeout(sendTimeout_)) {
          AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
              withAddr("failed to reschedule write timeout"));
          return failWrite(__func__, ex);
        }
      }
      return;
    }
  }
}

void AsyncSocket::checkForImmediateRead() noexcept {
  // We currently don't attempt to perform optimistic reads in AsyncSocket.
  // (However, note that some subclasses do override this method.)
  //
  // Simply calling handleRead() here would be bad, as this would call
  // readCallback_->getReadBuffer(), forcing the callback to allocate a read
  // buffer even though no data may be available.  This would waste lots of
  // memory, since the buffer will sit around unused until the socket actually
  // becomes readable.
  //
  // Checking if the socket is readable now also seems like it would probably
  // be a pessimism.  In most cases it probably wouldn't be readable, and we
  // would just waste an extra system call.  Even if it is readable, waiting to
  // find out from libevent on the next event loop doesn't seem that bad.
}

void AsyncSocket::handleInitialReadWrite() noexcept {
  // Our callers should already be holding a DestructorGuard, but grab
  // one here just to make sure, in case one of our calling code paths ever
  // changes.
  DestructorGuard dg(this);

  // If we have a readCallback_, make sure we enable read events.  We
  // may already be registered for reads if connectSuccess() set
  // the read calback.
  if (readCallback_ && !(eventFlags_ & EventHandler::READ)) {
    assert(state_ == StateEnum::ESTABLISHED);
    assert((shutdownFlags_ & SHUT_READ) == 0);
    if (!updateEventRegistration(EventHandler::READ, 0)) {
      assert(state_ == StateEnum::ERROR);
      return;
    }
    checkForImmediateRead();
  } else if (readCallback_ == nullptr) {
    // Unregister for read events.
    updateEventRegistration(0, EventHandler::READ);
  }

  // If we have write requests pending, try to send them immediately.
  // Since we just finished accepting, there is a very good chance that we can
  // write without blocking.
  //
  // However, we only process them if EventHandler::WRITE is not already set,
  // which means that we're already blocked on a write attempt.  (This can
  // happen if connectSuccess() called write() before returning.)
  if (writeReqHead_ && !(eventFlags_ & EventHandler::WRITE)) {
    // Call handleWrite() to perform write processing.
    handleWrite();
  } else if (writeReqHead_ == nullptr) {
    // Unregister for write event.
    updateEventRegistration(0, EventHandler::WRITE);
  }
}

void AsyncSocket::handleConnect() noexcept {
  VLOG(5) << "AsyncSocket::handleConnect() this=" << this << ", fd=" << fd_
          << ", state=" << state_;
  assert(state_ == StateEnum::CONNECTING);
  // SHUT_WRITE can never be set while we are still connecting;
  // SHUT_WRITE_PENDING may be set, be we only set SHUT_WRITE once the connect
  // finishes
  assert((shutdownFlags_ & SHUT_WRITE) == 0);

  // In case we had a connect timeout, cancel the timeout
  writeTimeout_.cancelTimeout();
  // We don't use a persistent registration when waiting on a connect event,
  // so we have been automatically unregistered now.  Update eventFlags_ to
  // reflect reality.
  assert(eventFlags_ == EventHandler::WRITE);
  eventFlags_ = EventHandler::NONE;

  // Call getsockopt() to check if the connect succeeded
  int error;
  socklen_t len = sizeof(error);
  int rv = getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len);
  if (rv != 0) {
    AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                           withAddr("error calling getsockopt() after connect"),
                           errno);
    VLOG(4) << "AsyncSocket::handleConnect(this=" << this << ", fd="
               << fd_ << " host=" << addr_.describe()
               << ") exception:" << ex.what();
    return failConnect(__func__, ex);
  }

  if (error != 0) {
    AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                           "connect failed", error);
    VLOG(1) << "AsyncSocket::handleConnect(this=" << this << ", fd="
            << fd_ << " host=" << addr_.describe()
            << ") exception: " << ex.what();
    return failConnect(__func__, ex);
  }

  // Move into STATE_ESTABLISHED
  state_ = StateEnum::ESTABLISHED;

  // If SHUT_WRITE_PENDING is set and we don't have any write requests to
  // perform, immediately shutdown the write half of the socket.
  if ((shutdownFlags_ & SHUT_WRITE_PENDING) && writeReqHead_ == nullptr) {
    // SHUT_READ shouldn't be set.  If close() is called on the socket while we
    // are still connecting we just abort the connect rather than waiting for
    // it to complete.
    assert((shutdownFlags_ & SHUT_READ) == 0);
    ::shutdown(fd_, SHUT_WR);
    shutdownFlags_ |= SHUT_WRITE;
  }

  VLOG(7) << "AsyncSocket " << this << ": fd " << fd_
          << "successfully connected; state=" << state_;

  // Remember the EventBase we are attached to, before we start invoking any
  // callbacks (since the callbacks may call detachEventBase()).
  EventBase* originalEventBase = eventBase_;

  // Call the connect callback.
  if (connectCallback_) {
    ConnectCallback* callback = connectCallback_;
    connectCallback_ = nullptr;
    callback->connectSuccess();
  }

  // Note that the connect callback may have changed our state.
  // (set or unset the read callback, called write(), closed the socket, etc.)
  // The following code needs to handle these situations correctly.
  //
  // If the socket has been closed, readCallback_ and writeReqHead_ will
  // always be nullptr, so that will prevent us from trying to read or write.
  //
  // The main thing to check for is if eventBase_ is still originalEventBase.
  // If not, we have been detached from this event base, so we shouldn't
  // perform any more operations.
  if (eventBase_ != originalEventBase) {
    return;
  }

  handleInitialReadWrite();
}

void AsyncSocket::timeoutExpired() noexcept {
  VLOG(7) << "AsyncSocket " << this << ", fd " << fd_ << ": timeout expired: "
          << "state=" << state_ << ", events=" << std::hex << eventFlags_;
  DestructorGuard dg(this);
  assert(eventBase_->isInEventBaseThread());

  if (state_ == StateEnum::CONNECTING) {
    // connect() timed out
    // Unregister for I/O events.
    AsyncSocketException ex(AsyncSocketException::TIMED_OUT,
                           "connect timed out");
    failConnect(__func__, ex);
  } else {
    // a normal write operation timed out
    assert(state_ == StateEnum::ESTABLISHED);
    AsyncSocketException ex(AsyncSocketException::TIMED_OUT, "write timed out");
    failWrite(__func__, ex);
  }
}

ssize_t AsyncSocket::performWrite(const iovec* vec,
                                   uint32_t count,
                                   WriteFlags flags,
                                   uint32_t* countWritten,
                                   uint32_t* partialWritten) {
  // We use sendmsg() instead of writev() so that we can pass in MSG_NOSIGNAL
  // We correctly handle EPIPE errors, so we never want to receive SIGPIPE
  // (since it may terminate the program if the main program doesn't explicitly
  // ignore it).
  struct msghdr msg;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = const_cast<iovec *>(vec);
#ifdef IOV_MAX // not defined on Android
  msg.msg_iovlen = std::min(count, (uint32_t)IOV_MAX);
#else
  msg.msg_iovlen = std::min(count, (uint32_t)UIO_MAXIOV);
#endif
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  int msg_flags = MSG_DONTWAIT;

#ifdef MSG_NOSIGNAL // Linux-only
  msg_flags |= MSG_NOSIGNAL;
  if (isSet(flags, WriteFlags::CORK)) {
    // MSG_MORE tells the kernel we have more data to send, so wait for us to
    // give it the rest of the data rather than immediately sending a partial
    // frame, even when TCP_NODELAY is enabled.
    msg_flags |= MSG_MORE;
  }
#endif
  if (isSet(flags, WriteFlags::EOR)) {
    // marks that this is the last byte of a record (response)
    msg_flags |= MSG_EOR;
  }
  ssize_t totalWritten = ::sendmsg(fd_, &msg, msg_flags);
  if (totalWritten < 0) {
    if (errno == EAGAIN) {
      // TCP buffer is full; we can't write any more data right now.
      *countWritten = 0;
      *partialWritten = 0;
      return 0;
    }
    // error
    *countWritten = 0;
    *partialWritten = 0;
    return -1;
  }

  appBytesWritten_ += totalWritten;

  uint32_t bytesWritten;
  uint32_t n;
  for (bytesWritten = totalWritten, n = 0; n < count; ++n) {
    const iovec* v = vec + n;
    if (v->iov_len > bytesWritten) {
      // Partial write finished in the middle of this iovec
      *countWritten = n;
      *partialWritten = bytesWritten;
      return totalWritten;
    }

    bytesWritten -= v->iov_len;
  }

  assert(bytesWritten == 0);
  *countWritten = n;
  *partialWritten = 0;
  return totalWritten;
}

/**
 * Re-register the EventHandler after eventFlags_ has changed.
 *
 * If an error occurs, fail() is called to move the socket into the error state
 * and call all currently installed callbacks.  After an error, the
 * AsyncSocket is completely unregistered.
 *
 * @return Returns true on succcess, or false on error.
 */
bool AsyncSocket::updateEventRegistration() {
  VLOG(5) << "AsyncSocket::updateEventRegistration(this=" << this
          << ", fd=" << fd_ << ", evb=" << eventBase_ << ", state=" << state_
          << ", events=" << std::hex << eventFlags_;
  assert(eventBase_->isInEventBaseThread());
  if (eventFlags_ == EventHandler::NONE) {
    ioHandler_.unregisterHandler();
    return true;
  }

  // Always register for persistent events, so we don't have to re-register
  // after being called back.
  if (!ioHandler_.registerHandler(eventFlags_ | EventHandler::PERSIST)) {
    eventFlags_ = EventHandler::NONE; // we're not registered after error
    AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
        withAddr("failed to update AsyncSocket event registration"));
    fail("updateEventRegistration", ex);
    return false;
  }

  return true;
}

bool AsyncSocket::updateEventRegistration(uint16_t enable,
                                           uint16_t disable) {
  uint16_t oldFlags = eventFlags_;
  eventFlags_ |= enable;
  eventFlags_ &= ~disable;
  if (eventFlags_ == oldFlags) {
    return true;
  } else {
    return updateEventRegistration();
  }
}

void AsyncSocket::startFail() {
  // startFail() should only be called once
  assert(state_ != StateEnum::ERROR);
  assert(getDestructorGuardCount() > 0);
  state_ = StateEnum::ERROR;
  // Ensure that SHUT_READ and SHUT_WRITE are set,
  // so all future attempts to read or write will be rejected
  shutdownFlags_ |= (SHUT_READ | SHUT_WRITE);

  if (eventFlags_ != EventHandler::NONE) {
    eventFlags_ = EventHandler::NONE;
    ioHandler_.unregisterHandler();
  }
  writeTimeout_.cancelTimeout();

  if (fd_ >= 0) {
    ioHandler_.changeHandlerFD(-1);
    doClose();
  }
}

void AsyncSocket::finishFail() {
  assert(state_ == StateEnum::ERROR);
  assert(getDestructorGuardCount() > 0);

  AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                         withAddr("socket closing after error"));
  if (connectCallback_) {
    ConnectCallback* callback = connectCallback_;
    connectCallback_ = nullptr;
    callback->connectErr(ex);
  }

  failAllWrites(ex);

  if (readCallback_) {
    ReadCallback* callback = readCallback_;
    readCallback_ = nullptr;
    callback->readErr(ex);
  }
}

void AsyncSocket::fail(const char* fn, const AsyncSocketException& ex) {
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_ << ", state="
             << state_ << " host=" << addr_.describe()
             << "): failed in " << fn << "(): "
             << ex.what();
  startFail();
  finishFail();
}

void AsyncSocket::failConnect(const char* fn, const AsyncSocketException& ex) {
  VLOG(5) << "AsyncSocket(this=" << this << ", fd=" << fd_ << ", state="
               << state_ << " host=" << addr_.describe()
               << "): failed while connecting in " << fn << "(): "
               << ex.what();
  startFail();

  if (connectCallback_ != nullptr) {
    ConnectCallback* callback = connectCallback_;
    connectCallback_ = nullptr;
    callback->connectErr(ex);
  }

  finishFail();
}

void AsyncSocket::failRead(const char* fn, const AsyncSocketException& ex) {
  VLOG(5) << "AsyncSocket(this=" << this << ", fd=" << fd_ << ", state="
               << state_ << " host=" << addr_.describe()
               << "): failed while reading in " << fn << "(): "
               << ex.what();
  startFail();

  if (readCallback_ != nullptr) {
    ReadCallback* callback = readCallback_;
    readCallback_ = nullptr;
    callback->readErr(ex);
  }

  finishFail();
}

void AsyncSocket::failWrite(const char* fn, const AsyncSocketException& ex) {
  VLOG(5) << "AsyncSocket(this=" << this << ", fd=" << fd_ << ", state="
               << state_ << " host=" << addr_.describe()
               << "): failed while writing in " << fn << "(): "
               << ex.what();
  startFail();

  // Only invoke the first write callback, since the error occurred while
  // writing this request.  Let any other pending write callbacks be invoked in
  // finishFail().
  if (writeReqHead_ != nullptr) {
    WriteRequest* req = writeReqHead_;
    writeReqHead_ = req->getNext();
    WriteCallback* callback = req->getCallback();
    uint32_t bytesWritten = req->getTotalBytesWritten();
    req->destroy();
    if (callback) {
      callback->writeErr(bytesWritten, ex);
    }
  }

  finishFail();
}

void AsyncSocket::failWrite(const char* fn, WriteCallback* callback,
                             size_t bytesWritten,
                             const AsyncSocketException& ex) {
  // This version of failWrite() is used when the failure occurs before
  // we've added the callback to writeReqHead_.
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_ << ", state="
             << state_ << " host=" << addr_.describe()
             <<"): failed while writing in " << fn << "(): "
             << ex.what();
  startFail();

  if (callback != nullptr) {
    callback->writeErr(bytesWritten, ex);
  }

  finishFail();
}

void AsyncSocket::failAllWrites(const AsyncSocketException& ex) {
  // Invoke writeError() on all write callbacks.
  // This is used when writes are forcibly shutdown with write requests
  // pending, or when an error occurs with writes pending.
  while (writeReqHead_ != nullptr) {
    WriteRequest* req = writeReqHead_;
    writeReqHead_ = req->getNext();
    WriteCallback* callback = req->getCallback();
    if (callback) {
      callback->writeErr(req->getTotalBytesWritten(), ex);
    }
    req->destroy();
  }
}

void AsyncSocket::invalidState(ConnectCallback* callback) {
  VLOG(5) << "AsyncSocket(this=" << this << ", fd=" << fd_
             << "): connect() called in invalid state " << state_;

  /*
   * The invalidState() methods don't use the normal failure mechanisms,
   * since we don't know what state we are in.  We don't want to call
   * startFail()/finishFail() recursively if we are already in the middle of
   * cleaning up.
   */

  AsyncSocketException ex(AsyncSocketException::ALREADY_OPEN,
                         "connect() called with socket in invalid state");
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->connectErr(ex);
    }
  } else {
    // We can't use failConnect() here since connectCallback_
    // may already be set to another callback.  Invoke this ConnectCallback
    // here; any other connectCallback_ will be invoked in finishFail()
    startFail();
    if (callback) {
      callback->connectErr(ex);
    }
    finishFail();
  }
}

void AsyncSocket::invalidState(ReadCallback* callback) {
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_
             << "): setReadCallback(" << callback
             << ") called in invalid state " << state_;

  AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                         "setReadCallback() called with socket in "
                         "invalid state");
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->readErr(ex);
    }
  } else {
    startFail();
    if (callback) {
      callback->readErr(ex);
    }
    finishFail();
  }
}

void AsyncSocket::invalidState(WriteCallback* callback) {
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_
             << "): write() called in invalid state " << state_;

  AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                         withAddr("write() called with socket in invalid state"));
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->writeErr(0, ex);
    }
  } else {
    startFail();
    if (callback) {
      callback->writeErr(0, ex);
    }
    finishFail();
  }
}

void AsyncSocket::doClose() {
  if (fd_ == -1) return;
  if (shutdownSocketSet_) {
    shutdownSocketSet_->close(fd_);
  } else {
    ::close(fd_);
  }
  fd_ = -1;
}

std::ostream& operator << (std::ostream& os,
                           const AsyncSocket::StateEnum& state) {
  os << static_cast<int>(state);
  return os;
}

std::string AsyncSocket::withAddr(const std::string& s) {
  // Don't use addr_ directly because it may not be initialized
  // e.g. if constructed from fd
  folly::SocketAddress peer, local;
  try {
    getPeerAddress(&peer);
    getLocalAddress(&local);
  } catch (const std::exception&) {
    // ignore
  } catch (...) {
    // ignore
  }
  return s + " (peer=" + peer.describe() + ", local=" + local.describe() + ")";
}

} // folly
