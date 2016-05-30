/*
 * Copyright 2016 Facebook, Inc.
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

#include <signal.h>
#include <pthread.h>

#include <folly/ExceptionWrapper.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ssl/SSLErrors.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>

#include <gtest/gtest.h>
#include <iostream>
#include <list>
#include <fcntl.h>
#include <sys/types.h>

namespace folly {

enum StateEnum {
  STATE_WAITING,
  STATE_SUCCEEDED,
  STATE_FAILED
};

// The destructors of all callback classes assert that the state is
// STATE_SUCCEEDED, for both possitive and negative tests. The tests
// are responsible for setting the succeeded state properly before the
// destructors are called.

class WriteCallbackBase :
public AsyncTransportWrapper::WriteCallback {
public:
  WriteCallbackBase()
      : state(STATE_WAITING)
      , bytesWritten(0)
      , exception(AsyncSocketException::UNKNOWN, "none") {}

  ~WriteCallbackBase() {
    EXPECT_EQ(STATE_SUCCEEDED, state);
  }

  void setSocket(
    const std::shared_ptr<AsyncSSLSocket> &socket) {
    socket_ = socket;
  }

  void writeSuccess() noexcept override {
    std::cerr << "writeSuccess" << std::endl;
    state = STATE_SUCCEEDED;
  }

  void writeErr(
    size_t bytesWritten,
    const AsyncSocketException& ex) noexcept override {
    std::cerr << "writeError: bytesWritten " << bytesWritten
         << ", exception " << ex.what() << std::endl;

    state = STATE_FAILED;
    this->bytesWritten = bytesWritten;
    exception = ex;
    socket_->close();
    socket_->detachEventBase();
  }

  std::shared_ptr<AsyncSSLSocket> socket_;
  StateEnum state;
  size_t bytesWritten;
  AsyncSocketException exception;
};

class ReadCallbackBase :
public AsyncTransportWrapper::ReadCallback {
 public:
  explicit ReadCallbackBase(WriteCallbackBase* wcb)
      : wcb_(wcb), state(STATE_WAITING) {}

  ~ReadCallbackBase() {
    EXPECT_EQ(state, STATE_SUCCEEDED);
  }

  void setSocket(
    const std::shared_ptr<AsyncSSLSocket> &socket) {
    socket_ = socket;
  }

  void setState(StateEnum s) {
    state = s;
    if (wcb_) {
      wcb_->state = s;
    }
  }

  void readErr(
    const AsyncSocketException& ex) noexcept override {
    std::cerr << "readError " << ex.what() << std::endl;
    state = STATE_FAILED;
    socket_->close();
    socket_->detachEventBase();
  }

  void readEOF() noexcept override {
    std::cerr << "readEOF" << std::endl;

    socket_->close();
    socket_->detachEventBase();
  }

  std::shared_ptr<AsyncSSLSocket> socket_;
  WriteCallbackBase *wcb_;
  StateEnum state;
};

class ReadCallback : public ReadCallbackBase {
public:
  explicit ReadCallback(WriteCallbackBase *wcb)
      : ReadCallbackBase(wcb)
      , buffers() {}

  ~ReadCallback() {
    for (std::vector<Buffer>::iterator it = buffers.begin();
         it != buffers.end();
         ++it) {
      it->free();
    }
    currentBuffer.free();
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    if (!currentBuffer.buffer) {
      currentBuffer.allocate(4096);
    }
    *bufReturn = currentBuffer.buffer;
    *lenReturn = currentBuffer.length;
  }

  void readDataAvailable(size_t len) noexcept override {
    std::cerr << "readDataAvailable, len " << len << std::endl;

    currentBuffer.length = len;

    wcb_->setSocket(socket_);

    // Write back the same data.
    socket_->write(wcb_, currentBuffer.buffer, len);

    buffers.push_back(currentBuffer);
    currentBuffer.reset();
    state = STATE_SUCCEEDED;
  }

  class Buffer {
  public:
    Buffer() : buffer(nullptr), length(0) {}
    Buffer(char* buf, size_t len) : buffer(buf), length(len) {}

    void reset() {
      buffer = nullptr;
      length = 0;
    }
    void allocate(size_t length) {
      assert(buffer == nullptr);
      this->buffer = static_cast<char*>(malloc(length));
      this->length = length;
    }
    void free() {
      ::free(buffer);
      reset();
    }

    char* buffer;
    size_t length;
  };

  std::vector<Buffer> buffers;
  Buffer currentBuffer;
};

class ReadErrorCallback : public ReadCallbackBase {
public:
  explicit ReadErrorCallback(WriteCallbackBase *wcb)
      : ReadCallbackBase(wcb) {}

  // Return nullptr buffer to trigger readError()
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = nullptr;
    *lenReturn = 0;
  }

  void readDataAvailable(size_t /* len */) noexcept override {
    // This should never to called.
    FAIL();
  }

  void readErr(
    const AsyncSocketException& ex) noexcept override {
    ReadCallbackBase::readErr(ex);
    std::cerr << "ReadErrorCallback::readError" << std::endl;
    setState(STATE_SUCCEEDED);
  }
};

class ReadEOFCallback : public ReadCallbackBase {
 public:
  explicit ReadEOFCallback(WriteCallbackBase* wcb) : ReadCallbackBase(wcb) {}

  // Return nullptr buffer to trigger readError()
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = nullptr;
    *lenReturn = 0;
  }

  void readDataAvailable(size_t /* len */) noexcept override {
    // This should never to called.
    FAIL();
  }

  void readEOF() noexcept override {
    ReadCallbackBase::readEOF();
    setState(STATE_SUCCEEDED);
  }
};

class WriteErrorCallback : public ReadCallback {
public:
  explicit WriteErrorCallback(WriteCallbackBase *wcb)
      : ReadCallback(wcb) {}

  void readDataAvailable(size_t len) noexcept override {
    std::cerr << "readDataAvailable, len " << len << std::endl;

    currentBuffer.length = len;

    // close the socket before writing to trigger writeError().
    ::close(socket_->getFd());

    wcb_->setSocket(socket_);

    // Write back the same data.
    socket_->write(wcb_, currentBuffer.buffer, len);

    if (wcb_->state == STATE_FAILED) {
      setState(STATE_SUCCEEDED);
    } else {
      state = STATE_FAILED;
    }

    buffers.push_back(currentBuffer);
    currentBuffer.reset();
  }

  void readErr(const AsyncSocketException& ex) noexcept override {
    std::cerr << "readError " << ex.what() << std::endl;
    // do nothing since this is expected
  }
};

class EmptyReadCallback : public ReadCallback {
public:
  explicit EmptyReadCallback()
      : ReadCallback(nullptr) {}

  void readErr(const AsyncSocketException& ex) noexcept override {
    std::cerr << "readError " << ex.what() << std::endl;
    state = STATE_FAILED;
    tcpSocket_->close();
    tcpSocket_->detachEventBase();
  }

  void readEOF() noexcept override {
    std::cerr << "readEOF" << std::endl;

    tcpSocket_->close();
    tcpSocket_->detachEventBase();
    state = STATE_SUCCEEDED;
  }

  std::shared_ptr<AsyncSocket> tcpSocket_;
};

class HandshakeCallback :
public AsyncSSLSocket::HandshakeCB {
public:
  enum ExpectType {
    EXPECT_SUCCESS,
    EXPECT_ERROR
  };

  explicit HandshakeCallback(ReadCallbackBase *rcb,
                             ExpectType expect = EXPECT_SUCCESS):
      state(STATE_WAITING),
      rcb_(rcb),
      expect_(expect) {}

  void setSocket(
    const std::shared_ptr<AsyncSSLSocket> &socket) {
    socket_ = socket;
  }

  void setState(StateEnum s) {
    state = s;
    rcb_->setState(s);
  }

  // Functions inherited from AsyncSSLSocketHandshakeCallback
  void handshakeSuc(AsyncSSLSocket *sock) noexcept override {
    std::lock_guard<std::mutex> g(mutex_);
    cv_.notify_all();
    EXPECT_EQ(sock, socket_.get());
    std::cerr << "HandshakeCallback::connectionAccepted" << std::endl;
    rcb_->setSocket(socket_);
    sock->setReadCB(rcb_);
    state = (expect_ == EXPECT_SUCCESS) ? STATE_SUCCEEDED : STATE_FAILED;
  }
  void handshakeErr(AsyncSSLSocket* /* sock */,
                    const AsyncSocketException& ex) noexcept override {
    std::lock_guard<std::mutex> g(mutex_);
    cv_.notify_all();
    std::cerr << "HandshakeCallback::handshakeError " << ex.what() << std::endl;
    state = (expect_ == EXPECT_ERROR) ? STATE_SUCCEEDED : STATE_FAILED;
    if (expect_ == EXPECT_ERROR) {
      // rcb will never be invoked
      rcb_->setState(STATE_SUCCEEDED);
    }
    errorString_ = ex.what();
  }

  void waitForHandshake() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return state != STATE_WAITING; });
  }

  ~HandshakeCallback() {
    EXPECT_EQ(state, STATE_SUCCEEDED);
  }

  void closeSocket() {
    socket_->close();
    state = STATE_SUCCEEDED;
  }

  std::shared_ptr<AsyncSSLSocket> getSocket() {
    return socket_;
  }

  StateEnum state;
  std::shared_ptr<AsyncSSLSocket> socket_;
  ReadCallbackBase *rcb_;
  ExpectType expect_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::string errorString_;
};

class SSLServerAcceptCallbackBase:
public folly::AsyncServerSocket::AcceptCallback {
public:
  explicit SSLServerAcceptCallbackBase(HandshakeCallback *hcb):
  state(STATE_WAITING), hcb_(hcb) {}

  ~SSLServerAcceptCallbackBase() {
    EXPECT_EQ(state, STATE_SUCCEEDED);
  }

  void acceptError(const std::exception& ex) noexcept override {
    std::cerr << "SSLServerAcceptCallbackBase::acceptError "
              << ex.what() << std::endl;
    state = STATE_FAILED;
  }

  void connectionAccepted(
      int fd, const folly::SocketAddress& /* clientAddr */) noexcept override {
    printf("Connection accepted\n");
    std::shared_ptr<AsyncSSLSocket> sslSock;
    try {
      // Create a AsyncSSLSocket object with the fd. The socket should be
      // added to the event base and in the state of accepting SSL connection.
      sslSock = AsyncSSLSocket::newSocket(ctx_, base_, fd);
    } catch (const std::exception &e) {
      LOG(ERROR) << "Exception %s caught while creating a AsyncSSLSocket "
        "object with socket " << e.what() << fd;
      ::close(fd);
      acceptError(e);
      return;
    }

    connAccepted(sslSock);
  }

  virtual void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s) = 0;

  StateEnum state;
  HandshakeCallback *hcb_;
  std::shared_ptr<folly::SSLContext> ctx_;
  folly::EventBase* base_;
};

class SSLServerAcceptCallback: public SSLServerAcceptCallbackBase {
public:
  uint32_t timeout_;

  explicit SSLServerAcceptCallback(HandshakeCallback *hcb,
                                   uint32_t timeout = 0):
      SSLServerAcceptCallbackBase(hcb),
      timeout_(timeout) {}

  virtual ~SSLServerAcceptCallback() {
    if (timeout_ > 0) {
      // if we set a timeout, we expect failure
      EXPECT_EQ(hcb_->state, STATE_FAILED);
      hcb_->setState(STATE_SUCCEEDED);
    }
  }

  // Functions inherited from TAsyncSSLServerSocket::SSLAcceptCallback
  void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s)
    noexcept override {
    auto sock = std::static_pointer_cast<AsyncSSLSocket>(s);
    std::cerr << "SSLServerAcceptCallback::connAccepted" << std::endl;

    hcb_->setSocket(sock);
    sock->sslAccept(hcb_, timeout_);
    EXPECT_EQ(sock->getSSLState(),
                      AsyncSSLSocket::STATE_ACCEPTING);

    state = STATE_SUCCEEDED;
  }
};

class SSLServerAcceptCallbackDelay: public SSLServerAcceptCallback {
public:
  explicit SSLServerAcceptCallbackDelay(HandshakeCallback *hcb):
      SSLServerAcceptCallback(hcb) {}

  // Functions inherited from TAsyncSSLServerSocket::SSLAcceptCallback
  void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s)
    noexcept override {

    auto sock = std::static_pointer_cast<AsyncSSLSocket>(s);

    std::cerr << "SSLServerAcceptCallbackDelay::connAccepted"
              << std::endl;
    int fd = sock->getFd();

#ifndef TCP_NOPUSH
    {
    // The accepted connection should already have TCP_NODELAY set
    int value;
    socklen_t valueLength = sizeof(value);
    int rc = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &valueLength);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(value, 1);
    }
#endif

    // Unset the TCP_NODELAY option.
    int value = 0;
    socklen_t valueLength = sizeof(value);
    int rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, valueLength);
    EXPECT_EQ(rc, 0);

    rc = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &valueLength);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(value, 0);

    SSLServerAcceptCallback::connAccepted(sock);
  }
};

class SSLServerAsyncCacheAcceptCallback: public SSLServerAcceptCallback {
public:
  explicit SSLServerAsyncCacheAcceptCallback(HandshakeCallback *hcb,
                                             uint32_t timeout = 0):
    SSLServerAcceptCallback(hcb, timeout) {}

  // Functions inherited from TAsyncSSLServerSocket::SSLAcceptCallback
  void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s)
    noexcept override {
    auto sock = std::static_pointer_cast<AsyncSSLSocket>(s);

    std::cerr << "SSLServerAcceptCallback::connAccepted" << std::endl;

    hcb_->setSocket(sock);
    sock->sslAccept(hcb_, timeout_);
    ASSERT_TRUE((sock->getSSLState() ==
                 AsyncSSLSocket::STATE_ACCEPTING) ||
                (sock->getSSLState() ==
                 AsyncSSLSocket::STATE_CACHE_LOOKUP));

    state = STATE_SUCCEEDED;
  }
};


class HandshakeErrorCallback: public SSLServerAcceptCallbackBase {
public:
  explicit HandshakeErrorCallback(HandshakeCallback *hcb):
  SSLServerAcceptCallbackBase(hcb)  {}

  // Functions inherited from TAsyncSSLServerSocket::SSLAcceptCallback
  void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s)
    noexcept override {
    auto sock = std::static_pointer_cast<AsyncSSLSocket>(s);

    std::cerr << "HandshakeErrorCallback::connAccepted" << std::endl;

    // The first call to sslAccept() should succeed.
    hcb_->setSocket(sock);
    sock->sslAccept(hcb_);
    EXPECT_EQ(sock->getSSLState(),
                      AsyncSSLSocket::STATE_ACCEPTING);

    // The second call to sslAccept() should fail.
    HandshakeCallback callback2(hcb_->rcb_);
    callback2.setSocket(sock);
    sock->sslAccept(&callback2);
    EXPECT_EQ(sock->getSSLState(),
                      AsyncSSLSocket::STATE_ERROR);

    // Both callbacks should be in the error state.
    EXPECT_EQ(hcb_->state, STATE_FAILED);
    EXPECT_EQ(callback2.state, STATE_FAILED);

    sock->detachEventBase();

    state = STATE_SUCCEEDED;
    hcb_->setState(STATE_SUCCEEDED);
    callback2.setState(STATE_SUCCEEDED);
  }
};

class HandshakeTimeoutCallback: public SSLServerAcceptCallbackBase {
public:
  explicit HandshakeTimeoutCallback(HandshakeCallback *hcb):
  SSLServerAcceptCallbackBase(hcb)  {}

  // Functions inherited from TAsyncSSLServerSocket::SSLAcceptCallback
  void connAccepted(
    const std::shared_ptr<folly::AsyncSSLSocket> &s)
    noexcept override {
    std::cerr << "HandshakeErrorCallback::connAccepted" << std::endl;

    auto sock = std::static_pointer_cast<AsyncSSLSocket>(s);

    hcb_->setSocket(sock);
    sock->getEventBase()->tryRunAfterDelay([=] {
        std::cerr << "Delayed SSL accept, client will have close by now"
                  << std::endl;
        // SSL accept will fail
        EXPECT_EQ(
          sock->getSSLState(),
          AsyncSSLSocket::STATE_UNINIT);
        hcb_->socket_->sslAccept(hcb_);
        // This registers for an event
        EXPECT_EQ(
          sock->getSSLState(),
          AsyncSSLSocket::STATE_ACCEPTING);

        state = STATE_SUCCEEDED;
      }, 100);
  }
};


class TestSSLServer {
 protected:
  EventBase evb_;
  std::shared_ptr<folly::SSLContext> ctx_;
  SSLServerAcceptCallbackBase *acb_;
  std::shared_ptr<folly::AsyncServerSocket> socket_;
  folly::SocketAddress address_;
  pthread_t thread_;

  static void *Main(void *ctx) {
    TestSSLServer *self = static_cast<TestSSLServer*>(ctx);
    self->evb_.loop();
    std::cerr << "Server thread exited event loop" << std::endl;
    return nullptr;
  }

 public:
  // Create a TestSSLServer.
  // This immediately starts listening on the given port.
  explicit TestSSLServer(SSLServerAcceptCallbackBase *acb);

  // Kill the thread.
  ~TestSSLServer() {
    evb_.runInEventBaseThread([&](){
      socket_->stopAccepting();
    });
    std::cerr << "Waiting for server thread to exit" << std::endl;
    pthread_join(thread_, nullptr);
  }

  EventBase &getEventBase() { return evb_; }

  const folly::SocketAddress& getAddress() const {
    return address_;
  }
};

class TestSSLAsyncCacheServer : public TestSSLServer {
 public:
  explicit TestSSLAsyncCacheServer(SSLServerAcceptCallbackBase *acb,
        int lookupDelay = 100) :
      TestSSLServer(acb) {
    SSL_CTX *sslCtx = ctx_->getSSLCtx();
    SSL_CTX_sess_set_get_cb(sslCtx,
                            TestSSLAsyncCacheServer::getSessionCallback);
    SSL_CTX_set_session_cache_mode(
      sslCtx, SSL_SESS_CACHE_NO_INTERNAL | SSL_SESS_CACHE_SERVER);
    asyncCallbacks_ = 0;
    asyncLookups_ = 0;
    lookupDelay_ = lookupDelay;
  }

  uint32_t getAsyncCallbacks() const { return asyncCallbacks_; }
  uint32_t getAsyncLookups() const { return asyncLookups_; }

 private:
  static uint32_t asyncCallbacks_;
  static uint32_t asyncLookups_;
  static uint32_t lookupDelay_;

  static SSL_SESSION* getSessionCallback(SSL* ssl,
                                         unsigned char* /* sess_id */,
                                         int /* id_len */,
                                         int* copyflag) {
    *copyflag = 0;
    asyncCallbacks_++;
#ifdef SSL_ERROR_WANT_SESS_CACHE_LOOKUP
    if (!SSL_want_sess_cache_lookup(ssl)) {
      // libssl.so mismatch
      std::cerr << "no async support" << std::endl;
      return nullptr;
    }

    AsyncSSLSocket *sslSocket =
        AsyncSSLSocket::getFromSSL(ssl);
    assert(sslSocket != nullptr);
    // Going to simulate an async cache by just running delaying the miss 100ms
    if (asyncCallbacks_ % 2 == 0) {
      // This socket is already blocked on lookup, return miss
      std::cerr << "returning miss" << std::endl;
    } else {
      // fresh meat - block it
      std::cerr << "async lookup" << std::endl;
      sslSocket->getEventBase()->tryRunAfterDelay(
        std::bind(&AsyncSSLSocket::restartSSLAccept,
                  sslSocket), lookupDelay_);
      *copyflag = SSL_SESSION_CB_WOULD_BLOCK;
      asyncLookups_++;
    }
#endif
    return nullptr;
  }
};

void getfds(int fds[2]);

void getctx(
  std::shared_ptr<folly::SSLContext> clientCtx,
  std::shared_ptr<folly::SSLContext> serverCtx);

void sslsocketpair(
  EventBase* eventBase,
  AsyncSSLSocket::UniquePtr* clientSock,
  AsyncSSLSocket::UniquePtr* serverSock);

class BlockingWriteClient :
  private AsyncSSLSocket::HandshakeCB,
  private AsyncTransportWrapper::WriteCallback {
 public:
  explicit BlockingWriteClient(
    AsyncSSLSocket::UniquePtr socket)
    : socket_(std::move(socket)),
      bufLen_(2500),
      iovCount_(2000) {
    // Fill buf_
    buf_.reset(new uint8_t[bufLen_]);
    for (uint32_t n = 0; n < sizeof(buf_); ++n) {
      buf_[n] = n % 0xff;
    }

    // Initialize iov_
    iov_.reset(new struct iovec[iovCount_]);
    for (uint32_t n = 0; n < iovCount_; ++n) {
      iov_[n].iov_base = buf_.get() + n;
      if (n & 0x1) {
        iov_[n].iov_len = n % bufLen_;
      } else {
        iov_[n].iov_len = bufLen_ - (n % bufLen_);
      }
    }

    socket_->sslConn(this, 100);
  }

  struct iovec* getIovec() const {
    return iov_.get();
  }
  uint32_t getIovecCount() const {
    return iovCount_;
  }

 private:
  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    socket_->writev(this, iov_.get(), iovCount_);
  }
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client handshake error: " << ex.what();
  }
  void writeSuccess() noexcept override {
    socket_->close();
  }
  void writeErr(
    size_t bytesWritten,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client write error after " << bytesWritten << " bytes: "
                  << ex.what();
  }

  AsyncSSLSocket::UniquePtr socket_;
  uint32_t bufLen_;
  uint32_t iovCount_;
  std::unique_ptr<uint8_t[]> buf_;
  std::unique_ptr<struct iovec[]> iov_;
};

class BlockingWriteServer :
    private AsyncSSLSocket::HandshakeCB,
    private AsyncTransportWrapper::ReadCallback {
 public:
  explicit BlockingWriteServer(
    AsyncSSLSocket::UniquePtr socket)
    : socket_(std::move(socket)),
      bufSize_(2500 * 2000),
      bytesRead_(0) {
    buf_.reset(new uint8_t[bufSize_]);
    socket_->sslAccept(this, 100);
  }

  void checkBuffer(struct iovec* iov, uint32_t count) const {
    uint32_t idx = 0;
    for (uint32_t n = 0; n < count; ++n) {
      size_t bytesLeft = bytesRead_ - idx;
      int rc = memcmp(buf_.get() + idx, iov[n].iov_base,
                      std::min(iov[n].iov_len, bytesLeft));
      if (rc != 0) {
        FAIL() << "buffer mismatch at iovec " << n << "/" << count
               << ": rc=" << rc;

      }
      if (iov[n].iov_len > bytesLeft) {
        FAIL() << "server did not read enough data: "
               << "ended at byte " << bytesLeft << "/" << iov[n].iov_len
               << " in iovec " << n << "/" << count;
      }

      idx += iov[n].iov_len;
    }
    if (idx != bytesRead_) {
      ADD_FAILURE() << "server read extra data: " << bytesRead_
                    << " bytes read; expected " << idx;
    }
  }

 private:
  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    // Wait 10ms before reading, so the client's writes will initially block.
    socket_->getEventBase()->tryRunAfterDelay(
        [this] { socket_->setReadCB(this); }, 10);
  }
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server handshake error: " << ex.what();
  }
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = buf_.get() + bytesRead_;
    *lenReturn = bufSize_ - bytesRead_;
  }
  void readDataAvailable(size_t len) noexcept override {
    bytesRead_ += len;
    socket_->setReadCB(nullptr);
    socket_->getEventBase()->tryRunAfterDelay(
        [this] { socket_->setReadCB(this); }, 2);
  }
  void readEOF() noexcept override {
    socket_->close();
  }
  void readErr(
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server read error: " << ex.what();
  }

  AsyncSSLSocket::UniquePtr socket_;
  uint32_t bufSize_;
  uint32_t bytesRead_;
  std::unique_ptr<uint8_t[]> buf_;
};

class NpnClient :
  private AsyncSSLSocket::HandshakeCB,
  private AsyncTransportWrapper::WriteCallback {
 public:
  explicit NpnClient(
    AsyncSSLSocket::UniquePtr socket)
      : nextProto(nullptr), nextProtoLength(0), socket_(std::move(socket)) {
    socket_->sslConn(this);
  }

  const unsigned char* nextProto;
  unsigned nextProtoLength;
  SSLContext::NextProtocolType protocolType;

 private:
  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    socket_->getSelectedNextProtocol(
        &nextProto, &nextProtoLength, &protocolType);
  }
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client handshake error: " << ex.what();
  }
  void writeSuccess() noexcept override {
    socket_->close();
  }
  void writeErr(
    size_t bytesWritten,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client write error after " << bytesWritten << " bytes: "
                  << ex.what();
  }

  AsyncSSLSocket::UniquePtr socket_;
};

class NpnServer :
    private AsyncSSLSocket::HandshakeCB,
    private AsyncTransportWrapper::ReadCallback {
 public:
  explicit NpnServer(AsyncSSLSocket::UniquePtr socket)
      : nextProto(nullptr), nextProtoLength(0), socket_(std::move(socket)) {
    socket_->sslAccept(this);
  }

  const unsigned char* nextProto;
  unsigned nextProtoLength;
  SSLContext::NextProtocolType protocolType;

 private:
  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    socket_->getSelectedNextProtocol(
        &nextProto, &nextProtoLength, &protocolType);
  }
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server handshake error: " << ex.what();
  }
  void getReadBuffer(void** /* bufReturn */, size_t* lenReturn) override {
    *lenReturn = 0;
  }
  void readDataAvailable(size_t /* len */) noexcept override {}
  void readEOF() noexcept override {
    socket_->close();
  }
  void readErr(
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server read error: " << ex.what();
  }

  AsyncSSLSocket::UniquePtr socket_;
};

class RenegotiatingServer : public AsyncSSLSocket::HandshakeCB,
                            public AsyncTransportWrapper::ReadCallback {
 public:
  explicit RenegotiatingServer(AsyncSSLSocket::UniquePtr socket)
      : socket_(std::move(socket)) {
    socket_->sslAccept(this);
  }

  ~RenegotiatingServer() {
    socket_->setReadCB(nullptr);
  }

  void handshakeSuc(AsyncSSLSocket* /* socket */) noexcept override {
    LOG(INFO) << "Renegotiating server handshake success";
    socket_->setReadCB(this);
  }
  void handshakeErr(
      AsyncSSLSocket*,
      const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "Renegotiating server handshake error: " << ex.what();
  }
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *lenReturn = sizeof(buf);
    *bufReturn = buf;
  }
  void readDataAvailable(size_t /* len */) noexcept override {}
  void readEOF() noexcept override {}
  void readErr(const AsyncSocketException& ex) noexcept override {
    LOG(INFO) << "server got read error " << ex.what();
    auto exPtr = dynamic_cast<const SSLException*>(&ex);
    ASSERT_NE(nullptr, exPtr);
    std::string exStr(ex.what());
    SSLException sslEx(SSLError::CLIENT_RENEGOTIATION);
    ASSERT_NE(std::string::npos, exStr.find(sslEx.what()));
    renegotiationError_ = true;
  }

  AsyncSSLSocket::UniquePtr socket_;
  unsigned char buf[128];
  bool renegotiationError_{false};
};

#ifndef OPENSSL_NO_TLSEXT
class SNIClient :
  private AsyncSSLSocket::HandshakeCB,
  private AsyncTransportWrapper::WriteCallback {
 public:
  explicit SNIClient(
    AsyncSSLSocket::UniquePtr socket)
      : serverNameMatch(false), socket_(std::move(socket)) {
    socket_->sslConn(this);
  }

  bool serverNameMatch;

 private:
  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    serverNameMatch = socket_->isServerNameMatch();
  }
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client handshake error: " << ex.what();
  }
  void writeSuccess() noexcept override {
    socket_->close();
  }
  void writeErr(
    size_t bytesWritten,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client write error after " << bytesWritten << " bytes: "
                  << ex.what();
  }

  AsyncSSLSocket::UniquePtr socket_;
};

class SNIServer :
    private AsyncSSLSocket::HandshakeCB,
    private AsyncTransportWrapper::ReadCallback {
 public:
  explicit SNIServer(
    AsyncSSLSocket::UniquePtr socket,
    const std::shared_ptr<folly::SSLContext>& ctx,
    const std::shared_ptr<folly::SSLContext>& sniCtx,
    const std::string& expectedServerName)
      : serverNameMatch(false), socket_(std::move(socket)), sniCtx_(sniCtx),
        expectedServerName_(expectedServerName) {
    ctx->setServerNameCallback(std::bind(&SNIServer::serverNameCallback, this,
                                         std::placeholders::_1));
    socket_->sslAccept(this);
  }

  bool serverNameMatch;

 private:
  void handshakeSuc(AsyncSSLSocket* /* ssl */) noexcept override {}
  void handshakeErr(
    AsyncSSLSocket*,
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server handshake error: " << ex.what();
  }
  void getReadBuffer(void** /* bufReturn */, size_t* lenReturn) override {
    *lenReturn = 0;
  }
  void readDataAvailable(size_t /* len */) noexcept override {}
  void readEOF() noexcept override {
    socket_->close();
  }
  void readErr(
    const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "server read error: " << ex.what();
  }

  folly::SSLContext::ServerNameCallbackResult
    serverNameCallback(SSL *ssl) {
    const char *sn = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (sniCtx_ &&
        sn &&
        !strcasecmp(expectedServerName_.c_str(), sn)) {
      AsyncSSLSocket *sslSocket =
          AsyncSSLSocket::getFromSSL(ssl);
      sslSocket->switchServerSSLContext(sniCtx_);
      serverNameMatch = true;
      return folly::SSLContext::SERVER_NAME_FOUND;
    } else {
      serverNameMatch = false;
      return folly::SSLContext::SERVER_NAME_NOT_FOUND;
    }
  }

  AsyncSSLSocket::UniquePtr socket_;
  std::shared_ptr<folly::SSLContext> sniCtx_;
  std::string expectedServerName_;
};
#endif

class SSLClient : public AsyncSocket::ConnectCallback,
                  public AsyncTransportWrapper::WriteCallback,
                  public AsyncTransportWrapper::ReadCallback
{
 private:
  EventBase *eventBase_;
  std::shared_ptr<AsyncSSLSocket> sslSocket_;
  SSL_SESSION *session_;
  std::shared_ptr<folly::SSLContext> ctx_;
  uint32_t requests_;
  folly::SocketAddress address_;
  uint32_t timeout_;
  char buf_[128];
  char readbuf_[128];
  uint32_t bytesRead_;
  uint32_t hit_;
  uint32_t miss_;
  uint32_t errors_;
  uint32_t writeAfterConnectErrors_;

  // These settings test that we eventually drain the
  // socket, even if the maxReadsPerEvent_ is hit during
  // a event loop iteration.
  static constexpr size_t kMaxReadsPerEvent = 2;
  static constexpr size_t kMaxReadBufferSz =
    sizeof(readbuf_) / kMaxReadsPerEvent / 2;  // 2 event loop iterations

 public:
  SSLClient(EventBase *eventBase,
            const folly::SocketAddress& address,
            uint32_t requests,
            uint32_t timeout = 0)
      : eventBase_(eventBase),
        session_(nullptr),
        requests_(requests),
        address_(address),
        timeout_(timeout),
        bytesRead_(0),
        hit_(0),
        miss_(0),
        errors_(0),
        writeAfterConnectErrors_(0) {
    ctx_.reset(new folly::SSLContext());
    ctx_->setOptions(SSL_OP_NO_TICKET);
    ctx_->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    memset(buf_, 'a', sizeof(buf_));
  }

  ~SSLClient() {
    if (session_) {
      SSL_SESSION_free(session_);
    }
    if (errors_ == 0) {
      EXPECT_EQ(bytesRead_, sizeof(buf_));
    }
  }

  uint32_t getHit() const { return hit_; }

  uint32_t getMiss() const { return miss_; }

  uint32_t getErrors() const { return errors_; }

  uint32_t getWriteAfterConnectErrors() const {
    return writeAfterConnectErrors_;
  }

  void connect(bool writeNow = false) {
    sslSocket_ = AsyncSSLSocket::newSocket(
      ctx_, eventBase_);
    if (session_ != nullptr) {
      sslSocket_->setSSLSession(session_);
    }
    requests_--;
    sslSocket_->connect(this, address_, timeout_);
    if (sslSocket_ && writeNow) {
      // write some junk, used in an error test
      sslSocket_->write(this, buf_, sizeof(buf_));
    }
  }

  void connectSuccess() noexcept override {
    std::cerr << "client SSL socket connected" << std::endl;
    if (sslSocket_->getSSLSessionReused()) {
      hit_++;
    } else {
      miss_++;
      if (session_ != nullptr) {
        SSL_SESSION_free(session_);
      }
      session_ = sslSocket_->getSSLSession();
    }

    // write()
    sslSocket_->setMaxReadsPerEvent(kMaxReadsPerEvent);
    sslSocket_->write(this, buf_, sizeof(buf_));
    sslSocket_->setReadCB(this);
    memset(readbuf_, 'b', sizeof(readbuf_));
    bytesRead_ = 0;
  }

  void connectErr(
    const AsyncSocketException& ex) noexcept override {
    std::cerr << "SSLClient::connectError: " << ex.what() << std::endl;
    errors_++;
    sslSocket_.reset();
  }

  void writeSuccess() noexcept override {
    std::cerr << "client write success" << std::endl;
  }

  void writeErr(size_t /* bytesWritten */,
                const AsyncSocketException& ex) noexcept override {
    std::cerr << "client writeError: " << ex.what() << std::endl;
    if (!sslSocket_) {
      writeAfterConnectErrors_++;
    }
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = readbuf_ + bytesRead_;
    *lenReturn = std::min(kMaxReadBufferSz, sizeof(readbuf_) - bytesRead_);
  }

  void readEOF() noexcept override {
    std::cerr << "client readEOF" << std::endl;
  }

  void readErr(
    const AsyncSocketException& ex) noexcept override {
    std::cerr << "client readError: " << ex.what() << std::endl;
  }

  void readDataAvailable(size_t len) noexcept override {
    std::cerr << "client read data: " << len << std::endl;
    bytesRead_ += len;
    if (bytesRead_ == sizeof(buf_)) {
      EXPECT_EQ(memcmp(buf_, readbuf_, bytesRead_), 0);
      sslSocket_->closeNow();
      sslSocket_.reset();
      if (requests_ != 0) {
        connect();
      }
    }
  }

};

class SSLHandshakeBase :
  public AsyncSSLSocket::HandshakeCB,
  private AsyncTransportWrapper::WriteCallback {
 public:
  explicit SSLHandshakeBase(
   AsyncSSLSocket::UniquePtr socket,
   bool preverifyResult,
   bool verifyResult) :
    handshakeVerify_(false),
    handshakeSuccess_(false),
    handshakeError_(false),
    socket_(std::move(socket)),
    preverifyResult_(preverifyResult),
    verifyResult_(verifyResult) {
  }

  AsyncSSLSocket::UniquePtr moveSocket() && {
    return std::move(socket_);
  }

  bool handshakeVerify_;
  bool handshakeSuccess_;
  bool handshakeError_;
  std::chrono::nanoseconds handshakeTime;

 protected:
  AsyncSSLSocket::UniquePtr socket_;
  bool preverifyResult_;
  bool verifyResult_;

  // HandshakeCallback
  bool handshakeVer(AsyncSSLSocket* /* sock */,
                    bool preverifyOk,
                    X509_STORE_CTX* /* ctx */) noexcept override {
    handshakeVerify_ = true;

    EXPECT_EQ(preverifyResult_, preverifyOk);
    return verifyResult_;
  }

  void handshakeSuc(AsyncSSLSocket*) noexcept override {
    LOG(INFO) << "Handshake success";
    handshakeSuccess_ = true;
    handshakeTime = socket_->getHandshakeTime();
  }

  void handshakeErr(
      AsyncSSLSocket*,
      const AsyncSocketException& ex) noexcept override {
    LOG(INFO) << "Handshake error " << ex.what();
    handshakeError_ = true;
    handshakeTime = socket_->getHandshakeTime();
  }

  // WriteCallback
  void writeSuccess() noexcept override {
    socket_->close();
  }

  void writeErr(
   size_t bytesWritten,
   const AsyncSocketException& ex) noexcept override {
    ADD_FAILURE() << "client write error after " << bytesWritten << " bytes: "
                  << ex.what();
  }
};

class SSLHandshakeClient : public SSLHandshakeBase {
 public:
  SSLHandshakeClient(
   AsyncSSLSocket::UniquePtr socket,
   bool preverifyResult,
   bool verifyResult) :
    SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslConn(this, 0);
  }
};

class SSLHandshakeClientNoVerify : public SSLHandshakeBase {
 public:
  SSLHandshakeClientNoVerify(
   AsyncSSLSocket::UniquePtr socket,
   bool preverifyResult,
   bool verifyResult) :
    SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslConn(this, 0,
      folly::SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  }
};

class SSLHandshakeClientDoVerify : public SSLHandshakeBase {
 public:
  SSLHandshakeClientDoVerify(
   AsyncSSLSocket::UniquePtr socket,
   bool preverifyResult,
   bool verifyResult) :
    SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslConn(this, 0,
      folly::SSLContext::SSLVerifyPeerEnum::VERIFY);
  }
};

class SSLHandshakeServer : public SSLHandshakeBase {
 public:
  SSLHandshakeServer(
      AsyncSSLSocket::UniquePtr socket,
      bool preverifyResult,
      bool verifyResult)
    : SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslAccept(this, 0);
  }
};

class SSLHandshakeServerParseClientHello : public SSLHandshakeBase {
 public:
  SSLHandshakeServerParseClientHello(
      AsyncSSLSocket::UniquePtr socket,
      bool preverifyResult,
      bool verifyResult)
      : SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->enableClientHelloParsing();
    socket_->sslAccept(this, 0);
  }

  std::string clientCiphers_, sharedCiphers_, serverCiphers_, chosenCipher_;

 protected:
  void handshakeSuc(AsyncSSLSocket* sock) noexcept override {
    handshakeSuccess_ = true;
    sock->getSSLSharedCiphers(sharedCiphers_);
    sock->getSSLServerCiphers(serverCiphers_);
    sock->getSSLClientCiphers(clientCiphers_);
    chosenCipher_ = sock->getNegotiatedCipherName();
  }
};


class SSLHandshakeServerNoVerify : public SSLHandshakeBase {
 public:
  SSLHandshakeServerNoVerify(
      AsyncSSLSocket::UniquePtr socket,
      bool preverifyResult,
      bool verifyResult)
    : SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslAccept(this, 0,
      folly::SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  }
};

class SSLHandshakeServerDoVerify : public SSLHandshakeBase {
 public:
  SSLHandshakeServerDoVerify(
      AsyncSSLSocket::UniquePtr socket,
      bool preverifyResult,
      bool verifyResult)
    : SSLHandshakeBase(std::move(socket), preverifyResult, verifyResult) {
    socket_->sslAccept(this, 0,
      folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
  }
};

class EventBaseAborter : public AsyncTimeout {
 public:
  EventBaseAborter(EventBase* eventBase,
                   uint32_t timeoutMS)
    : AsyncTimeout(
      eventBase, AsyncTimeout::InternalEnum::INTERNAL)
    , eventBase_(eventBase) {
    scheduleTimeout(timeoutMS);
  }

  void timeoutExpired() noexcept override {
    FAIL() << "test timed out";
    eventBase_->terminateLoopSoon();
  }

 private:
  EventBase* eventBase_;
};

}
