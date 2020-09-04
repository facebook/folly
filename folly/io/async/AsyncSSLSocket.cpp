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

#include <folly/io/async/AsyncSSLSocket.h>

#include <folly/io/async/EventBase.h>
#include <folly/portability/Sockets.h>

#include <fcntl.h>
#include <sys/types.h>
#include <cerrno>
#include <chrono>
#include <memory>
#include <utility>

#include <folly/Format.h>
#include <folly/Indestructible.h>
#include <folly/SocketAddress.h>
#include <folly/SpinLock.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/ssl/BasicTransportCertificate.h>
#include <folly/lang/Bits.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/SSLSession.h>
#include <folly/ssl/SSLSessionManager.h>

using std::shared_ptr;

using folly::SpinLock;
using folly::io::Cursor;
using folly::ssl::SSLSessionUniquePtr;

namespace {
using folly::AsyncSSLSocket;
using folly::SSLContext;
// For OpenSSL portability API
using namespace folly::ssl;
using folly::ssl::OpenSSLUtils;

// We have one single dummy SSL context so that we can implement attach
// and detach methods in a thread safe fashion without modifying opnessl.
SSLContext* dummyCtx = nullptr;
SpinLock dummyCtxLock;

// If given min write size is less than this, buffer will be allocated on
// stack, otherwise it is allocated on heap
const size_t MAX_STACK_BUF_SIZE = 2048;

// This converts "illegal" shutdowns into ZERO_RETURN
inline bool zero_return(int error, int rc, int errno_copy) {
  if (error == SSL_ERROR_ZERO_RETURN || (rc == 0 && errno_copy == 0)) {
    return true;
  }
#ifdef _WIN32
  // on windows underlying TCP socket may error with this code
  // if the sending/receiving client crashes or is killed
  if (error == SSL_ERROR_SYSCALL && errno_copy == WSAECONNRESET) {
    return true;
  }
#endif
  return false;
}

void setup_SSL_CTX(SSL_CTX* ctx) {
#ifdef SSL_MODE_RELEASE_BUFFERS
  SSL_CTX_set_mode(
      ctx,
      SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE |
          SSL_MODE_RELEASE_BUFFERS);
#else
  SSL_CTX_set_mode(
      ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);
#endif
// SSL_CTX_set_mode is a Macro
#ifdef SSL_MODE_WRITE_IOVEC
  SSL_CTX_set_mode(ctx, SSL_CTX_get_mode(ctx) | SSL_MODE_WRITE_IOVEC);
#endif
}

// Note: This is a Leaky Meyer's Singleton. The reason we can't use a non-leaky
// thing is because we will be setting this BIO_METHOD* inside BIOs owned by
// various SSL objects which may get callbacks even during teardown. We may
// eventually try to fix this
BIO_METHOD* getSSLBioMethod() {
  static auto const instance = OpenSSLUtils::newSocketBioMethod().release();
  return instance;
}

void* initsslBioMethod() {
  auto sslBioMethod = getSSLBioMethod();
  // override the bwrite method for MSG_EOR support
  OpenSSLUtils::setCustomBioWriteMethod(sslBioMethod, AsyncSSLSocket::bioWrite);
  OpenSSLUtils::setCustomBioReadMethod(sslBioMethod, AsyncSSLSocket::bioRead);

  // Note that the sslBioMethod.type and sslBioMethod.name are not
  // set here. openssl code seems to be checking ".type == BIO_TYPE_SOCKET" and
  // then have specific handlings. The sslWriteBioWrite should be compatible
  // with the one in openssl.

  // Return something here to enable AsyncSSLSocket to call this method using
  // a function-scoped static.
  return nullptr;
}

} // namespace

namespace folly {

class AsyncSSLSocketConnector : public AsyncSocket::ConnectCallback,
                                public AsyncSSLSocket::HandshakeCB {
 private:
  AsyncSSLSocket* sslSocket_;
  AsyncSSLSocket::ConnectCallback* callback_;
  std::chrono::milliseconds timeout_;
  std::chrono::steady_clock::time_point startTime_;

 public:
  AsyncSSLSocketConnector(
      AsyncSSLSocket* sslSocket,
      AsyncSocket::ConnectCallback* callback,
      std::chrono::milliseconds timeout)
      : sslSocket_(sslSocket),
        callback_(callback),
        timeout_(timeout),
        startTime_(std::chrono::steady_clock::now()) {}

  ~AsyncSSLSocketConnector() override = default;

  void preConnect(folly::NetworkSocket fd) override {
    VLOG(7) << "client preConnect hook is invoked";
    if (callback_) {
      callback_->preConnect(fd);
    }
  }

  void connectSuccess() noexcept override {
    VLOG(7) << "client socket connected";

    std::chrono::milliseconds timeoutLeft{0};
    if (timeout_ > std::chrono::milliseconds::zero()) {
      auto curTime = std::chrono::steady_clock::now();

      timeoutLeft = std::chrono::duration_cast<std::chrono::milliseconds>(
          timeout_ - (curTime - startTime_));
      if (timeoutLeft <= std::chrono::milliseconds::zero()) {
        AsyncSocketException ex(
            AsyncSocketException::TIMED_OUT,
            folly::sformat(
                "SSL connect timed out after {}ms", timeout_.count()));
        fail(ex);
        delete this;
        return;
      }
    }
    sslSocket_->sslConn(this, timeoutLeft);
  }

  void connectErr(const AsyncSocketException& ex) noexcept override {
    VLOG(1) << "TCP connect failed: " << ex.what();
    fail(ex);
    delete this;
  }

  void handshakeSuc(AsyncSSLSocket* /* sock */) noexcept override {
    VLOG(7) << "client handshake success";
    if (callback_) {
      callback_->connectSuccess();
    }
    delete this;
  }

  void handshakeErr(
      AsyncSSLSocket* /* socket */,
      const AsyncSocketException& ex) noexcept override {
    VLOG(1) << "client handshakeErr: " << ex.what();
    fail(ex);
    delete this;
  }

  void fail(const AsyncSocketException& ex) {
    // fail is a noop if called twice
    if (callback_) {
      AsyncSSLSocket::ConnectCallback* cb = callback_;
      callback_ = nullptr;

      cb->connectErr(ex);
      sslSocket_->closeNow();
      // closeNow can call handshakeErr if it hasn't been called already.
      // So this may have been deleted, no member variable access beyond this
      // point
      // Note that closeNow may invoke writeError callbacks if the socket had
      // write data pending connection completion.
    }
  }
};

/**
 * Create a client AsyncSSLSocket
 */
AsyncSSLSocket::AsyncSSLSocket(
    shared_ptr<SSLContext> ctx,
    EventBase* evb,
    bool deferSecurityNegotiation)
    : AsyncSocket(evb),
      ctx_(std::move(ctx)),
      handshakeTimeout_(this, evb),
      connectionTimeout_(this, evb) {
  init();
  if (deferSecurityNegotiation) {
    sslState_ = STATE_UNENCRYPTED;
  }
}

/**
 * Create a server/client AsyncSSLSocket
 */
AsyncSSLSocket::AsyncSSLSocket(
    shared_ptr<SSLContext> ctx,
    EventBase* evb,
    NetworkSocket fd,
    bool server,
    bool deferSecurityNegotiation)
    : AsyncSocket(evb, fd),
      server_(server),
      ctx_(std::move(ctx)),
      handshakeTimeout_(this, evb),
      connectionTimeout_(this, evb) {
  noTransparentTls_ = true;
  init();
  if (server) {
    SSL_CTX_set_info_callback(
        ctx_->getSSLCtx(), AsyncSSLSocket::sslInfoCallback);
  }
  if (deferSecurityNegotiation) {
    sslState_ = STATE_UNENCRYPTED;
  }
}

AsyncSSLSocket::AsyncSSLSocket(
    shared_ptr<SSLContext> ctx,
    AsyncSocket* oldAsyncSocket,
    bool server,
    bool deferSecurityNegotiation)
    : AsyncSocket(oldAsyncSocket),
      server_(server),
      ctx_(std::move(ctx)),
      handshakeTimeout_(this, AsyncSocket::getEventBase()),
      connectionTimeout_(this, AsyncSocket::getEventBase()) {
  noTransparentTls_ = true;
  init();
  if (server) {
    SSL_CTX_set_info_callback(
        ctx_->getSSLCtx(), AsyncSSLSocket::sslInfoCallback);
  }
  if (deferSecurityNegotiation) {
    sslState_ = STATE_UNENCRYPTED;
  }
}

AsyncSSLSocket::AsyncSSLSocket(
    shared_ptr<SSLContext> ctx,
    AsyncSocket::UniquePtr oldAsyncSocket,
    bool server,
    bool deferSecurityNegotiation)
    : AsyncSSLSocket(
          ctx,
          oldAsyncSocket.get(),
          server,
          deferSecurityNegotiation) {}

#if FOLLY_OPENSSL_HAS_SNI
/**
 * Create a client AsyncSSLSocket and allow tlsext_hostname
 * to be sent in Client Hello.
 */
AsyncSSLSocket::AsyncSSLSocket(
    const shared_ptr<SSLContext>& ctx,
    EventBase* evb,
    const std::string& serverName,
    bool deferSecurityNegotiation)
    : AsyncSSLSocket(ctx, evb, deferSecurityNegotiation) {
  tlsextHostname_ = serverName;
}

/**
 * Create a client AsyncSSLSocket from an already connected fd
 * and allow tlsext_hostname to be sent in Client Hello.
 */
AsyncSSLSocket::AsyncSSLSocket(
    const shared_ptr<SSLContext>& ctx,
    EventBase* evb,
    NetworkSocket fd,
    const std::string& serverName,
    bool deferSecurityNegotiation)
    : AsyncSSLSocket(ctx, evb, fd, false, deferSecurityNegotiation) {
  tlsextHostname_ = serverName;
}
#endif // FOLLY_OPENSSL_HAS_SNI

AsyncSSLSocket::~AsyncSSLSocket() {
  VLOG(3) << "actual destruction of AsyncSSLSocket(this=" << this
          << ", evb=" << eventBase_ << ", fd=" << fd_
          << ", state=" << int(state_) << ", sslState=" << sslState_
          << ", events=" << eventFlags_ << ")";
}

void AsyncSSLSocket::init() {
  // Do this here to ensure we initialize this once before any use of
  // AsyncSSLSocket instances and not as part of library load.
  static const auto sslBioMethodInitializer = initsslBioMethod();
  (void)sslBioMethodInitializer;

  setup_SSL_CTX(ctx_->getSSLCtx());
}

void AsyncSSLSocket::closeNow() {
  // Close the SSL connection.
  if (ssl_ != nullptr && fd_ != NetworkSocket() && !waitingOnAccept_) {
    int rc = SSL_shutdown(ssl_.get());
    if (rc == 0) {
      rc = SSL_shutdown(ssl_.get());
    }
    if (rc < 0) {
      ERR_clear_error();
    }
  }

  sslState_ = STATE_CLOSED;

  if (handshakeTimeout_.isScheduled()) {
    handshakeTimeout_.cancelTimeout();
  }

  DestructorGuard dg(this);

  static const Indestructible<AsyncSocketException> ex(
      AsyncSocketException::END_OF_FILE, "SSL connection closed locally");
  invokeHandshakeErr(*ex);

  // Close the socket.
  AsyncSocket::closeNow();
}

void AsyncSSLSocket::shutdownWrite() {
  // SSL sockets do not support half-shutdown, so just perform a full shutdown.
  //
  // (Performing a full shutdown here is more desirable than doing nothing at
  // all.  The purpose of shutdownWrite() is normally to notify the other end
  // of the connection that no more data will be sent.  If we do nothing, the
  // other end will never know that no more data is coming, and this may result
  // in protocol deadlock.)
  close();
}

void AsyncSSLSocket::shutdownWriteNow() {
  closeNow();
}

bool AsyncSSLSocket::good() const {
  return (
      AsyncSocket::good() &&
      (sslState_ == STATE_ACCEPTING || sslState_ == STATE_CONNECTING ||
       sslState_ == STATE_ESTABLISHED || sslState_ == STATE_UNENCRYPTED ||
       sslState_ == STATE_UNINIT));
}

// The AsyncTransport definition of 'good' states that the transport is
// ready to perform reads and writes, so sslState_ == UNINIT must report !good.
// connecting can be true when the sslState_ == UNINIT because the AsyncSocket
// is connected but we haven't initiated the call to SSL_connect.
bool AsyncSSLSocket::connecting() const {
  return (
      !server_ &&
      (AsyncSocket::connecting() ||
       (AsyncSocket::good() &&
        (sslState_ == STATE_UNINIT || sslState_ == STATE_CONNECTING))));
}

std::string AsyncSSLSocket::getApplicationProtocol() const noexcept {
  const unsigned char* protoName = nullptr;
  unsigned protoLength;
  if (getSelectedNextProtocolNoThrow(&protoName, &protoLength)) {
    return std::string(reinterpret_cast<const char*>(protoName), protoLength);
  }
  return "";
}

void AsyncSSLSocket::setEorTracking(bool track) {
  AsyncSocket::setEorTracking(track);
}

size_t AsyncSSLSocket::getRawBytesWritten() const {
  // The bio(s) in the write path are in a chain
  // each bio flushes to the next and finally written into the socket
  // to get the rawBytesWritten on the socket,
  // get the write bytes of the last bio
  BIO* b;
  if (!ssl_ || !(b = SSL_get_wbio(ssl_.get()))) {
    return 0;
  }
  BIO* next = BIO_next(b);
  while (next != nullptr) {
    b = next;
    next = BIO_next(b);
  }

  return BIO_number_written(b);
}

size_t AsyncSSLSocket::getRawBytesReceived() const {
  BIO* b;
  if (!ssl_ || !(b = SSL_get_rbio(ssl_.get()))) {
    return 0;
  }

  return BIO_number_read(b);
}

void AsyncSSLSocket::invalidState(HandshakeCB* callback) {
  LOG(ERROR) << "AsyncSSLSocket(this=" << this << ", fd=" << fd_
             << ", state=" << int(state_) << ", sslState=" << sslState_ << ", "
             << "events=" << eventFlags_ << ", server=" << short(server_)
             << "): "
             << "sslAccept/Connect() called in invalid "
             << "state, handshake callback " << handshakeCallback_
             << ", new callback " << callback;
  assert(!handshakeTimeout_.isScheduled());
  sslState_ = STATE_ERROR;

  static const Indestructible<AsyncSocketException> ex(
      AsyncSocketException::INVALID_STATE,
      "sslAccept() called with socket in invalid state");

  handshakeEndTime_ = std::chrono::steady_clock::now();
  if (callback) {
    callback->handshakeErr(this, *ex);
  }

  failHandshake(__func__, *ex);
}

void AsyncSSLSocket::sslAccept(
    HandshakeCB* callback,
    std::chrono::milliseconds timeout,
    const SSLContext::SSLVerifyPeerEnum& verifyPeer) {
  DestructorGuard dg(this);
  eventBase_->dcheckIsInEventBaseThread();
  verifyPeer_ = verifyPeer;

  // Make sure we're in the uninitialized state
  if (!server_ ||
      (sslState_ != STATE_UNINIT && sslState_ != STATE_UNENCRYPTED) ||
      handshakeCallback_ != nullptr) {
    return invalidState(callback);
  }

  // Cache local and remote socket addresses to keep them available
  // after socket file descriptor is closed.
  if (cacheAddrOnFailure_) {
    cacheAddresses();
  }

  handshakeStartTime_ = std::chrono::steady_clock::now();
  // Make end time at least >= start time.
  handshakeEndTime_ = handshakeStartTime_;

  sslState_ = STATE_ACCEPTING;
  handshakeCallback_ = callback;

  if (timeout > std::chrono::milliseconds::zero()) {
    handshakeTimeout_.scheduleTimeout(timeout);
  }

  /* register for a read operation (waiting for CLIENT HELLO) */
  updateEventRegistration(EventHandler::READ, EventHandler::WRITE);

  checkForImmediateRead();
}

void AsyncSSLSocket::attachSSLContext(const std::shared_ptr<SSLContext>& ctx) {
  // Check to ensure we are in client mode. Changing a server's ssl
  // context doesn't make sense since clients of that server would likely
  // become confused when the server's context changes.
  DCHECK(!server_);
  DCHECK(!ctx_);
  DCHECK(ctx);
  DCHECK(ctx->getSSLCtx());
  ctx_ = ctx;

  // It's possible this could be attached before ssl_ is set up
  if (!ssl_) {
    return;
  }

  // In order to call attachSSLContext, detachSSLContext must have been
  // previously called.
  // We need to update the initial_ctx if necessary
  // The 'initial_ctx' inside an SSL* points to the context that it was created
  // with, which is also where session callbacks and servername callbacks
  // happen.
  // When we switch to a different SSL_CTX, we want to update the initial_ctx as
  // well so that any callbacks don't go to a different object
  // NOTE: this will only work if we have access to ssl_ internals, so it may
  // not work on
  // OpenSSL version >= 1.1.0
  auto sslCtx = ctx->getSSLCtx();
  OpenSSLUtils::setSSLInitialCtx(ssl_.get(), sslCtx);
  // Detach sets the socket's context to the dummy context. Thus we must acquire
  // this lock.
  SpinLockGuard guard(dummyCtxLock);
  SSL_set_SSL_CTX(ssl_.get(), sslCtx);
}

void AsyncSSLSocket::detachSSLContext() {
  DCHECK(ctx_);
  ctx_.reset();
  // It's possible for this to be called before ssl_ has been
  // set up
  if (!ssl_) {
    return;
  }
  // The 'initial_ctx' inside an SSL* points to the context that it was created
  // with, which is also where session callbacks and servername callbacks
  // happen.
  // Detach the initial_ctx as well.  It will be reattached in attachSSLContext
  // it is used for session info.
  // NOTE: this will only work if we have access to ssl_ internals, so it may
  // not work on
  // OpenSSL version >= 1.1.0
  SSL_CTX* initialCtx = OpenSSLUtils::getSSLInitialCtx(ssl_.get());
  if (initialCtx) {
    SSL_CTX_free(initialCtx);
    OpenSSLUtils::setSSLInitialCtx(ssl_.get(), nullptr);
  }

  SpinLockGuard guard(dummyCtxLock);
  if (nullptr == dummyCtx) {
    // We need to lazily initialize the dummy context so we don't
    // accidentally override any programmatic settings to openssl
    dummyCtx = new SSLContext;
  }
  // We must remove this socket's references to its context right now
  // since this socket could get passed to any thread. If the context has
  // had its locking disabled, just doing a set in attachSSLContext()
  // would not be thread safe.
  SSL_set_SSL_CTX(ssl_.get(), dummyCtx->getSSLCtx());
}

#if FOLLY_OPENSSL_HAS_SNI
void AsyncSSLSocket::switchServerSSLContext(
    const std::shared_ptr<SSLContext>& handshakeCtx) {
  CHECK(server_);
  if (sslState_ != STATE_ACCEPTING) {
    // We log it here and allow the switch.
    // It should not affect our re-negotiation support (which
    // is not supported now).
    VLOG(6) << "fd=" << getNetworkSocket()
            << " renegotation detected when switching SSL_CTX";
  }

  setup_SSL_CTX(handshakeCtx->getSSLCtx());
  SSL_CTX_set_info_callback(
      handshakeCtx->getSSLCtx(), AsyncSSLSocket::sslInfoCallback);
  handshakeCtx_ = handshakeCtx;
  SSL_set_SSL_CTX(ssl_.get(), handshakeCtx->getSSLCtx());
}

bool AsyncSSLSocket::isServerNameMatch() const {
  CHECK(!server_);

  if (!ssl_) {
    return false;
  }

  SSL_SESSION* ss = SSL_get_session(ssl_.get());
  if (!ss) {
    return false;
  }

  auto tlsextHostname = SSL_SESSION_get0_hostname(ss);
  return (tlsextHostname && !tlsextHostname_.compare(tlsextHostname));
}

void AsyncSSLSocket::setServerName(std::string serverName) noexcept {
  tlsextHostname_ = std::move(serverName);
}

#endif // FOLLY_OPENSSL_HAS_SNI

void AsyncSSLSocket::timeoutExpired(
    std::chrono::milliseconds timeout) noexcept {
  if (state_ == StateEnum::ESTABLISHED && sslState_ == STATE_ASYNC_PENDING) {
    sslState_ = STATE_ERROR;
    // We are expecting a callback in restartSSLAccept.  The cache lookup
    // and rsa-call necessarily have pointers to this ssl socket, so delay
    // the cleanup until he calls us back.
  } else if (state_ == StateEnum::CONNECTING) {
    assert(sslState_ == STATE_CONNECTING);
    DestructorGuard dg(this);
    static const Indestructible<AsyncSocketException> ex(
        AsyncSocketException::TIMED_OUT,
        "Fallback connect timed out during TFO");
    failHandshake(__func__, *ex);
  } else {
    assert(
        state_ == StateEnum::ESTABLISHED &&
        (sslState_ == STATE_CONNECTING || sslState_ == STATE_ACCEPTING));
    DestructorGuard dg(this);
    AsyncSocketException ex(
        AsyncSocketException::TIMED_OUT,
        folly::sformat(
            "SSL {} timed out after {}ms",
            (sslState_ == STATE_CONNECTING) ? "connect" : "accept",
            timeout.count()));
    failHandshake(__func__, ex);
  }
}

int AsyncSSLSocket::getSSLExDataIndex() {
  static auto index = SSL_get_ex_new_index(
      0, (void*)"AsyncSSLSocket data index", nullptr, nullptr, nullptr);
  return index;
}

AsyncSSLSocket* AsyncSSLSocket::getFromSSL(const SSL* ssl) {
  return static_cast<AsyncSSLSocket*>(
      SSL_get_ex_data(ssl, getSSLExDataIndex()));
}

void AsyncSSLSocket::failHandshake(
    const char* /* fn */,
    const AsyncSocketException& ex) {
  startFail();
  if (handshakeTimeout_.isScheduled()) {
    handshakeTimeout_.cancelTimeout();
  }
  invokeHandshakeErr(ex);
  finishFail();
}

void AsyncSSLSocket::invokeHandshakeErr(const AsyncSocketException& ex) {
  handshakeEndTime_ = std::chrono::steady_clock::now();
  if (handshakeCallback_ != nullptr) {
    HandshakeCB* callback = handshakeCallback_;
    handshakeCallback_ = nullptr;
    callback->handshakeErr(this, ex);
  }
}

void AsyncSSLSocket::invokeHandshakeCB() {
  handshakeEndTime_ = std::chrono::steady_clock::now();
  if (handshakeTimeout_.isScheduled()) {
    handshakeTimeout_.cancelTimeout();
  }
  if (handshakeCallback_) {
    HandshakeCB* callback = handshakeCallback_;
    handshakeCallback_ = nullptr;
    callback->handshakeSuc(this);
  }
}

void AsyncSSLSocket::connect(
    ConnectCallback* callback,
    const folly::SocketAddress& address,
    int timeout,
    const SocketOptionMap& options,
    const folly::SocketAddress& bindAddr) noexcept {
  auto timeoutChrono = std::chrono::milliseconds(timeout);
  connect(callback, address, timeoutChrono, timeoutChrono, options, bindAddr);
}

void AsyncSSLSocket::connect(
    ConnectCallback* callback,
    const folly::SocketAddress& address,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds totalConnectTimeout,
    const SocketOptionMap& options,
    const folly::SocketAddress& bindAddr) noexcept {
  assert(!server_);
  assert(state_ == StateEnum::UNINIT);
  assert(sslState_ == STATE_UNINIT || sslState_ == STATE_UNENCRYPTED);
  noTransparentTls_ = true;
  totalConnectTimeout_ = totalConnectTimeout;
  if (sslState_ != STATE_UNENCRYPTED) {
    allocatedConnectCallback_ =
        new AsyncSSLSocketConnector(this, callback, totalConnectTimeout);
    callback = allocatedConnectCallback_;
  }
  AsyncSocket::connect(
      callback, address, int(connectTimeout.count()), options, bindAddr);
}

void AsyncSSLSocket::cancelConnect() {
  if (connectCallback_ && allocatedConnectCallback_) {
    // Since the connect callback won't be called, clean it up.
    delete allocatedConnectCallback_;
    allocatedConnectCallback_ = nullptr;
    connectCallback_ = nullptr;
  }
  AsyncSocket::cancelConnect();
}

bool AsyncSSLSocket::needsPeerVerification() const {
  if (verifyPeer_ == SSLContext::SSLVerifyPeerEnum::USE_CTX) {
    return ctx_->needsPeerVerification();
  }
  return (
      verifyPeer_ == SSLContext::SSLVerifyPeerEnum::VERIFY ||
      verifyPeer_ == SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
}

bool AsyncSSLSocket::applyVerificationOptions(const ssl::SSLUniquePtr& ssl) {
  // apply the settings specified in verifyPeer_
  if (verifyPeer_ == SSLContext::SSLVerifyPeerEnum::USE_CTX) {
    if (ctx_->needsPeerVerification()) {
      if (ctx_->checkPeerName()) {
#if FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101
        return false;
#else
        std::string peerNameToVerify = !ctx_->peerFixedName().empty()
            ? ctx_->peerFixedName()
            : tlsextHostname_;

        X509_VERIFY_PARAM* param = SSL_get0_param(ssl.get());
        if (!X509_VERIFY_PARAM_set1_host(
                param, peerNameToVerify.c_str(), peerNameToVerify.length())) {
          return false;
        }
#endif // FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101
      }

      SSL_set_verify(
          ssl.get(),
          ctx_->getVerificationMode(),
          AsyncSSLSocket::sslVerifyCallback);
    }
  } else {
    if (verifyPeer_ == SSLContext::SSLVerifyPeerEnum::VERIFY ||
        verifyPeer_ == SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT) {
      SSL_set_verify(
          ssl.get(),
          SSLContext::getVerificationMode(verifyPeer_),
          AsyncSSLSocket::sslVerifyCallback);
    }
  }

  return true;
}

bool AsyncSSLSocket::setupSSLBio() {
  auto sslBio = BIO_new(getSSLBioMethod());

  if (!sslBio) {
    return false;
  }

  OpenSSLUtils::setBioAppData(sslBio, this);
  OpenSSLUtils::setBioFd(sslBio, fd_, BIO_NOCLOSE);
  SSL_set_bio(ssl_.get(), sslBio, sslBio);
  return true;
}

void AsyncSSLSocket::sslConn(
    HandshakeCB* callback,
    std::chrono::milliseconds timeout,
    const SSLContext::SSLVerifyPeerEnum& verifyPeer) {
  DestructorGuard dg(this);
  eventBase_->dcheckIsInEventBaseThread();

  // Cache local and remote socket addresses to keep them available
  // after socket file descriptor is closed.
  if (cacheAddrOnFailure_) {
    cacheAddresses();
  }

  verifyPeer_ = verifyPeer;

  // Make sure we're in the uninitialized state
  if (server_ ||
      (sslState_ != STATE_UNINIT && sslState_ != STATE_UNENCRYPTED) ||
      handshakeCallback_ != nullptr) {
    return invalidState(callback);
  }

  sslState_ = STATE_CONNECTING;
  handshakeCallback_ = callback;

  try {
    ssl_.reset(ctx_->createSSL());
  } catch (std::exception& e) {
    sslState_ = STATE_ERROR;
    static const Indestructible<AsyncSocketException> ex(
        AsyncSocketException::INTERNAL_ERROR,
        "error calling SSLContext::createSSL()");
    LOG(ERROR) << "AsyncSSLSocket::sslConn(this=" << this << ", fd=" << fd_
               << "): " << e.what();
    return failHandshake(__func__, *ex);
  }

  if (!setupSSLBio()) {
    sslState_ = STATE_ERROR;
    static const Indestructible<AsyncSocketException> ex(
        AsyncSocketException::INTERNAL_ERROR, "error creating SSL bio");
    return failHandshake(__func__, *ex);
  }

  if (!applyVerificationOptions(ssl_)) {
    sslState_ = STATE_ERROR;
    static const Indestructible<AsyncSocketException> ex(
        AsyncSocketException::INTERNAL_ERROR,
        "error applying the SSL verification options");
    return failHandshake(__func__, *ex);
  }

  SSLSessionUniquePtr sessionPtr = sslSessionManager_.getRawSession();
  if (sessionPtr) {
    sessionResumptionAttempted_ = true;
    SSL_set_session(ssl_.get(), sessionPtr.get());
  }
#if FOLLY_OPENSSL_HAS_SNI
  if (!tlsextHostname_.empty()) {
    SSL_set_tlsext_host_name(ssl_.get(), tlsextHostname_.c_str());
  }
#endif

  SSL_set_ex_data(ssl_.get(), getSSLExDataIndex(), this);
  sslSessionManager_.attachToSSL(ssl_.get());

  handshakeConnectTimeout_ = timeout;
  startSSLConnect();
}

// This could be called multiple times, during normal ssl connections
// and after TFO fallback.
void AsyncSSLSocket::startSSLConnect() {
  handshakeStartTime_ = std::chrono::steady_clock::now();
  // Make end time at least >= start time.
  handshakeEndTime_ = handshakeStartTime_;
  if (handshakeConnectTimeout_ > std::chrono::milliseconds::zero()) {
    handshakeTimeout_.scheduleTimeout(handshakeConnectTimeout_);
  }
  handleConnect();
}

SSL_SESSION* AsyncSSLSocket::getSSLSession() {
  if (ssl_ != nullptr && sslState_ == STATE_ESTABLISHED) {
    return SSL_get1_session(ssl_.get());
  }

  return sslSessionManager_.getRawSession().release();
}

shared_ptr<ssl::SSLSession> AsyncSSLSocket::getSSLSessionV2() {
  return sslSessionManager_.getSession();
}

const SSL* AsyncSSLSocket::getSSL() const {
  return ssl_.get();
}

void AsyncSSLSocket::setSSLSession(SSL_SESSION* session, bool takeOwnership) {
  if (!takeOwnership && session != nullptr) {
    // Increment the reference count
    // This API exists in BoringSSL and OpenSSL 1.1.0
    SSL_SESSION_up_ref(session);
  }
  sslSessionManager_.setRawSession(SSLSessionUniquePtr(session));
}

void AsyncSSLSocket::setSSLSessionV2(shared_ptr<ssl::SSLSession> session) {
  sslSessionManager_.setSession(session);
}

void AsyncSSLSocket::getSelectedNextProtocol(
    const unsigned char** protoName,
    unsigned* protoLen) const {
  if (!getSelectedNextProtocolNoThrow(protoName, protoLen)) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_SUPPORTED, "ALPN not supported");
  }
}

bool AsyncSSLSocket::getSelectedNextProtocolNoThrow(
    const unsigned char** protoName,
    unsigned* protoLen) const {
  *protoName = nullptr;
  *protoLen = 0;
#if FOLLY_OPENSSL_HAS_ALPN
  SSL_get0_alpn_selected(ssl_.get(), protoName, protoLen);
  return true;
#else
  return false;
#endif
}

bool AsyncSSLSocket::getSSLSessionReused() const {
  if (ssl_ != nullptr && sslState_ == STATE_ESTABLISHED) {
    return SSL_session_reused(ssl_.get());
  }
  return false;
}

const char* AsyncSSLSocket::getNegotiatedCipherName() const {
  return (ssl_ != nullptr) ? SSL_get_cipher_name(ssl_.get()) : nullptr;
}

/* static */
const char* AsyncSSLSocket::getSSLServerNameFromSSL(SSL* ssl) {
  if (ssl == nullptr) {
    return nullptr;
  }
#ifdef SSL_CTRL_SET_TLSEXT_SERVERNAME_CB
  return SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
#else
  return nullptr;
#endif
}

const char* AsyncSSLSocket::getSSLServerName() const {
  if (clientHelloInfo_ && !clientHelloInfo_->clientHelloSNIHostname_.empty()) {
    return clientHelloInfo_->clientHelloSNIHostname_.c_str();
  }
#ifdef SSL_CTRL_SET_TLSEXT_SERVERNAME_CB
  return getSSLServerNameFromSSL(ssl_.get());
#else
  throw AsyncSocketException(
      AsyncSocketException::NOT_SUPPORTED, "SNI not supported");
#endif
}

const char* AsyncSSLSocket::getSSLServerNameNoThrow() const {
  if (clientHelloInfo_ && !clientHelloInfo_->clientHelloSNIHostname_.empty()) {
    return clientHelloInfo_->clientHelloSNIHostname_.c_str();
  }
  return getSSLServerNameFromSSL(ssl_.get());
}

int AsyncSSLSocket::getSSLVersion() const {
  return (ssl_ != nullptr) ? SSL_version(ssl_.get()) : 0;
}

const char* AsyncSSLSocket::getSSLCertSigAlgName() const {
  X509* cert = (ssl_ != nullptr) ? SSL_get_certificate(ssl_.get()) : nullptr;
  if (cert) {
    int nid = X509_get_signature_nid(cert);
    return OBJ_nid2ln(nid);
  }
  return nullptr;
}

int AsyncSSLSocket::getSSLCertSize() const {
  int certSize = 0;
  X509* cert = (ssl_ != nullptr) ? SSL_get_certificate(ssl_.get()) : nullptr;
  if (cert) {
    EVP_PKEY* key = X509_get_pubkey(cert);
    certSize = EVP_PKEY_bits(key);
    EVP_PKEY_free(key);
  }
  return certSize;
}

const AsyncTransportCertificate* AsyncSSLSocket::getPeerCertificate() const {
  if (peerCertData_) {
    return peerCertData_.get();
  }
  if (ssl_ != nullptr) {
    auto peerX509 = SSL_get_peer_certificate(ssl_.get());
    if (peerX509) {
      // already up ref'd
      folly::ssl::X509UniquePtr peer(peerX509);
      auto cn = OpenSSLUtils::getCommonName(peerX509);
      peerCertData_ = std::make_unique<BasicTransportCertificate>(
          std::move(cn), std::move(peer));
    }
  }
  return peerCertData_.get();
}

const AsyncTransportCertificate* AsyncSSLSocket::getSelfCertificate() const {
  if (selfCertData_) {
    return selfCertData_.get();
  }
  if (ssl_ != nullptr) {
    auto selfX509 = SSL_get_certificate(ssl_.get());
    if (selfX509) {
      // need to upref
      X509_up_ref(selfX509);
      folly::ssl::X509UniquePtr peer(selfX509);
      auto cn = OpenSSLUtils::getCommonName(selfX509);
      selfCertData_ = std::make_unique<BasicTransportCertificate>(
          std::move(cn), std::move(peer));
    }
  }
  return selfCertData_.get();
}

bool AsyncSSLSocket::willBlock(
    int ret,
    int* sslErrorOut,
    unsigned long* errErrorOut) noexcept {
  *errErrorOut = 0;
  int error = *sslErrorOut = sslGetErrorImpl(ssl_.get(), ret);
  if (error == SSL_ERROR_WANT_READ) {
    // Register for read event if not already.
    updateEventRegistration(EventHandler::READ, EventHandler::WRITE);
    return true;
  }
  if (error == SSL_ERROR_WANT_WRITE) {
    VLOG(3) << "AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
            << ", sslState=" << sslState_ << ", events=" << eventFlags_ << "): "
            << "SSL_ERROR_WANT_WRITE";
    // Register for write event if not already.
    updateEventRegistration(EventHandler::WRITE, EventHandler::READ);
    return true;
  }
  if ((false
#ifdef SSL_ERROR_WANT_ASYNC // OpenSSL 1.1.0 Async API
       || error == SSL_ERROR_WANT_ASYNC
#endif
       )) {
    // An asynchronous request has been kicked off. On completion, it will
    // invoke a callback to re-call handleAccept
    sslState_ = STATE_ASYNC_PENDING;

    // Unregister for all events while blocked here
    updateEventRegistration(
        EventHandler::NONE, EventHandler::READ | EventHandler::WRITE);

#ifdef SSL_ERROR_WANT_ASYNC
    if (error == SSL_ERROR_WANT_ASYNC) {
      size_t numfds;
      if (SSL_get_all_async_fds(ssl_.get(), nullptr, &numfds) <= 0) {
        VLOG(4) << "SSL_ERROR_WANT_ASYNC but no async FDs set!";
        return false;
      }
      if (numfds != 1) {
        VLOG(4) << "SSL_ERROR_WANT_ASYNC expected exactly 1 async fd, got "
                << numfds;
        return false;
      }
      OSSL_ASYNC_FD ofd; // This should just be an int in POSIX
      if (SSL_get_all_async_fds(ssl_.get(), &ofd, &numfds) <= 0) {
        VLOG(4) << "SSL_ERROR_WANT_ASYNC cant get async fd";
        return false;
      }

      // On POSIX systems, OSSL_ASYNC_FD is type int, but on win32
      // it has type HANDLE.
      // Our NetworkSocket::native_handle_type is type SOCKET on
      // win32, which means that we need to explicitly construct
      // a native handle type to pass to the constructor.
      auto native_handle = NetworkSocket::native_handle_type(ofd);

      auto asyncPipeReader =
          AsyncPipeReader::newReader(eventBase_, NetworkSocket(native_handle));
      auto asyncPipeReaderPtr = asyncPipeReader.get();
      if (!asyncOperationFinishCallback_) {
        asyncOperationFinishCallback_.reset(
            new DefaultOpenSSLAsyncFinishCallback(
                std::move(asyncPipeReader), this, DestructorGuard(this)));
      }
      asyncPipeReaderPtr->setReadCB(asyncOperationFinishCallback_.get());
    }
#endif

    // The timeout (if set) keeps running here
    return true;
  } else {
    unsigned long lastError = *errErrorOut = ERR_get_error();
    VLOG(6) << "AsyncSSLSocket(fd=" << fd_ << ", "
            << "state=" << state_ << ", "
            << "sslState=" << sslState_ << ", "
            << "events=" << std::hex << eventFlags_ << "): "
            << "SSL error: " << error << ", "
            << "errno: " << errno << ", "
            << "ret: " << ret << ", "
            << "read: " << BIO_number_read(SSL_get_rbio(ssl_.get())) << ", "
            << "written: " << BIO_number_written(SSL_get_wbio(ssl_.get()))
            << ", "
            << "func: " << ERR_func_error_string(lastError) << ", "
            << "reason: " << ERR_reason_error_string(lastError);
    return false;
  }
}

void AsyncSSLSocket::checkForImmediateRead() noexcept {
  // openssl may have buffered data that it read from the socket already.
  // In this case we have to process it immediately, rather than waiting for
  // the socket to become readable again.
  if (ssl_ != nullptr && SSL_pending(ssl_.get()) > 0) {
    AsyncSocket::handleRead();
  } else {
    AsyncSocket::checkForImmediateRead();
  }
}

void AsyncSSLSocket::restartSSLAccept() {
  VLOG(3) << "AsyncSSLSocket::restartSSLAccept() this=" << this
          << ", fd=" << fd_ << ", state=" << int(state_) << ", "
          << "sslState=" << sslState_ << ", events=" << eventFlags_;
  DestructorGuard dg(this);
  assert(
      sslState_ == STATE_ASYNC_PENDING || sslState_ == STATE_ERROR ||
      sslState_ == STATE_CLOSED);
  if (sslState_ == STATE_CLOSED) {
    // I sure hope whoever closed this socket didn't delete it already,
    // but this is not strictly speaking an error
    return;
  }
  if (sslState_ == STATE_ERROR) {
    // go straight to fail if timeout expired during lookup
    static const Indestructible<AsyncSocketException> ex(
        AsyncSocketException::TIMED_OUT, "SSL accept timed out");
    failHandshake(__func__, *ex);
    return;
  }
  sslState_ = STATE_ACCEPTING;
  this->handleAccept();
}

void AsyncSSLSocket::handleAccept() noexcept {
  VLOG(3) << "AsyncSSLSocket::handleAccept() this=" << this << ", fd=" << fd_
          << ", state=" << int(state_) << ", "
          << "sslState=" << sslState_ << ", events=" << eventFlags_;
  assert(server_);
  assert(state_ == StateEnum::ESTABLISHED && sslState_ == STATE_ACCEPTING);
  if (!ssl_) {
    /* lazily create the SSL structure */
    try {
      ssl_.reset(ctx_->createSSL());
    } catch (std::exception& e) {
      sslState_ = STATE_ERROR;
      static const Indestructible<AsyncSocketException> ex(
          AsyncSocketException::INTERNAL_ERROR,
          "error calling SSLContext::createSSL()");
      LOG(ERROR) << "AsyncSSLSocket::handleAccept(this=" << this
                 << ", fd=" << fd_ << "): " << e.what();
      return failHandshake(__func__, *ex);
    }

    if (!setupSSLBio()) {
      sslState_ = STATE_ERROR;
      static const Indestructible<AsyncSocketException> ex(
          AsyncSocketException::INTERNAL_ERROR, "error creating write bio");
      return failHandshake(__func__, *ex);
    }

    SSL_set_ex_data(ssl_.get(), getSSLExDataIndex(), this);

    if (!applyVerificationOptions(ssl_)) {
      sslState_ = STATE_ERROR;
      static const Indestructible<AsyncSocketException> ex(
          AsyncSocketException::INTERNAL_ERROR,
          "error applying the SSL verification options");
      return failHandshake(__func__, *ex);
    }
  }

  if (server_ && parseClientHello_) {
    SSL_set_msg_callback(
        ssl_.get(), &AsyncSSLSocket::clientHelloParsingCallback);
    SSL_set_msg_callback_arg(ssl_.get(), this);
  }

  DCHECK(ctx_->sslAcceptRunner());
  updateEventRegistration(
      EventHandler::NONE, EventHandler::READ | EventHandler::WRITE);
  DelayedDestruction::DestructorGuard dg(this);
  ctx_->sslAcceptRunner()->run(
      [this, dg]() {
        waitingOnAccept_ = true;
        return SSL_accept(ssl_.get());
      },
      [this, dg](int ret) {
        waitingOnAccept_ = false;
        handleReturnFromSSLAccept(ret);
      });
}

void AsyncSSLSocket::handleReturnFromSSLAccept(int ret) {
  if (sslState_ != STATE_ACCEPTING) {
    return;
  }

  if (ret <= 0) {
    VLOG(3) << "SSL_accept returned: " << ret;
    int sslError;
    unsigned long errError;
    int errnoCopy = errno;
    if (willBlock(ret, &sslError, &errError)) {
      return;
    } else {
      sslState_ = STATE_ERROR;
      SSLException ex(sslError, errError, ret, errnoCopy);
      return failHandshake(__func__, ex);
    }
  }

  handshakeComplete_ = true;
  updateEventRegistration(0, EventHandler::READ | EventHandler::WRITE);

  // Move into STATE_ESTABLISHED in the normal case that we are in
  // STATE_ACCEPTING.
  sslState_ = STATE_ESTABLISHED;

  VLOG(3) << "AsyncSSLSocket " << this << ": fd " << fd_
          << " successfully accepted; state=" << int(state_)
          << ", sslState=" << sslState_ << ", events=" << eventFlags_;

  // Remember the EventBase we are attached to, before we start invoking any
  // callbacks (since the callbacks may call detachEventBase()).
  EventBase* originalEventBase = eventBase_;

  // Call the accept callback.
  invokeHandshakeCB();

  // Note that the accept callback may have changed our state.
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

  AsyncSocket::handleInitialReadWrite();
}

void AsyncSSLSocket::handleConnect() noexcept {
  VLOG(3) << "AsyncSSLSocket::handleConnect() this=" << this << ", fd=" << fd_
          << ", state=" << int(state_) << ", "
          << "sslState=" << sslState_ << ", events=" << eventFlags_;
  assert(!server_);
  if (state_ < StateEnum::ESTABLISHED) {
    return AsyncSocket::handleConnect();
  }

  assert(
      (state_ == StateEnum::FAST_OPEN || state_ == StateEnum::ESTABLISHED) &&
      sslState_ == STATE_CONNECTING);
  assert(ssl_);

  auto originalState = state_;
  int ret = SSL_connect(ssl_.get());
  if (ret <= 0) {
    int sslError;
    unsigned long errError;
    int errnoCopy = errno;
    if (willBlock(ret, &sslError, &errError)) {
      // We fell back to connecting state due to TFO
      if (state_ == StateEnum::CONNECTING) {
        DCHECK_EQ(StateEnum::FAST_OPEN, originalState);
        if (handshakeTimeout_.isScheduled()) {
          handshakeTimeout_.cancelTimeout();
        }
      }
      return;
    } else {
      sslState_ = STATE_ERROR;
      SSLException ex(sslError, errError, ret, errnoCopy);
      return failHandshake(__func__, ex);
    }
  }

  handshakeComplete_ = true;
  updateEventRegistration(0, EventHandler::READ | EventHandler::WRITE);

  // Move into STATE_ESTABLISHED in the normal case that we are in
  // STATE_CONNECTING.
  sslState_ = STATE_ESTABLISHED;

  VLOG(3) << "AsyncSSLSocket " << this << ": "
          << "fd " << fd_ << " successfully connected; "
          << "state=" << int(state_) << ", sslState=" << sslState_
          << ", events=" << eventFlags_;

  // Remember the EventBase we are attached to, before we start invoking any
  // callbacks (since the callbacks may call detachEventBase()).
  EventBase* originalEventBase = eventBase_;

  // Call the handshake callback.
  invokeHandshakeCB();

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

  AsyncSocket::handleInitialReadWrite();
}

void AsyncSSLSocket::invokeConnectErr(const AsyncSocketException& ex) {
  connectionTimeout_.cancelTimeout();
  AsyncSocket::invokeConnectErr(ex);
  if (sslState_ == SSLStateEnum::STATE_CONNECTING) {
    if (handshakeTimeout_.isScheduled()) {
      handshakeTimeout_.cancelTimeout();
    }
    // If we fell back to connecting state during TFO and the connection
    // failed, it would be an SSL failure as well.
    invokeHandshakeErr(ex);
  }
}

void AsyncSSLSocket::invokeConnectSuccess() {
  connectionTimeout_.cancelTimeout();
  if (sslState_ == SSLStateEnum::STATE_CONNECTING) {
    assert(tfoAttempted_);
    // If we failed TFO, we'd fall back to trying to connect the socket,
    // to setup things like timeouts.
    startSSLConnect();
  }
  // still invoke the base class since it re-sets the connect time.
  AsyncSocket::invokeConnectSuccess();
}

void AsyncSSLSocket::scheduleConnectTimeout() {
  if (sslState_ == SSLStateEnum::STATE_CONNECTING) {
    // We fell back from TFO, and need to set the timeouts.
    // We will not have a connect callback in this case, thus if the timer
    // expires we would have no-one to notify.
    // Thus we should reset even the connect timers to point to the handshake
    // timeouts.
    assert(connectCallback_ == nullptr);
    // We use a different connect timeout here than the handshake timeout, so
    // that we can disambiguate the 2 timers.
    if (connectTimeout_.count() > 0) {
      if (!connectionTimeout_.scheduleTimeout(connectTimeout_)) {
        throw AsyncSocketException(
            AsyncSocketException::INTERNAL_ERROR,
            withAddr("failed to schedule AsyncSSLSocket connect timeout"));
      }
    }
    return;
  }
  AsyncSocket::scheduleConnectTimeout();
}

void AsyncSSLSocket::handleRead() noexcept {
  VLOG(5) << "AsyncSSLSocket::handleRead() this=" << this << ", fd=" << fd_
          << ", state=" << int(state_) << ", "
          << "sslState=" << sslState_ << ", events=" << eventFlags_;
  if (state_ < StateEnum::ESTABLISHED) {
    return AsyncSocket::handleRead();
  }

  if (sslState_ == STATE_ACCEPTING) {
    assert(server_);
    handleAccept();
    return;
  } else if (sslState_ == STATE_CONNECTING) {
    assert(!server_);
    handleConnect();
    return;
  }

  // Normal read
  AsyncSocket::handleRead();
}

AsyncSocket::ReadResult
AsyncSSLSocket::performRead(void** buf, size_t* buflen, size_t* offset) {
  VLOG(4) << "AsyncSSLSocket::performRead() this=" << this << ", buf=" << *buf
          << ", buflen=" << *buflen;

  if (sslState_ == STATE_UNENCRYPTED) {
    return AsyncSocket::performRead(buf, buflen, offset);
  }

  int numToRead = 0;
  if (*buflen > std::numeric_limits<int>::max()) {
    numToRead = std::numeric_limits<int>::max();
    VLOG(4) << "Clamping SSL_read to " << numToRead;
  } else {
    numToRead = int(*buflen);
  }
  int bytes = SSL_read(ssl_.get(), *buf, numToRead);

  if (server_ && renegotiateAttempted_) {
    LOG(ERROR) << "AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
               << ", sslstate=" << sslState_ << ", events=" << eventFlags_
               << "): client intitiated SSL renegotiation not permitted";
    return ReadResult(
        READ_ERROR,
        std::make_unique<SSLException>(SSLError::CLIENT_RENEGOTIATION));
  }
  if (bytes <= 0) {
    int error = sslGetErrorImpl(ssl_.get(), bytes);
    if (error == SSL_ERROR_WANT_READ) {
      // The caller will register for read event if not already.
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return ReadResult(READ_BLOCKING);
      } else {
        return ReadResult(READ_ERROR);
      }
    } else if (error == SSL_ERROR_WANT_WRITE) {
      // TODO: Even though we are attempting to read data, SSL_read() may
      // need to write data if renegotiation is being performed.  We currently
      // don't support this and just fail the read.
      LOG(ERROR) << "AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
                 << ", sslState=" << sslState_ << ", events=" << eventFlags_
                 << "): unsupported SSL renegotiation during read";
      return ReadResult(
          READ_ERROR,
          std::make_unique<SSLException>(SSLError::INVALID_RENEGOTIATION));
    } else {
      if (zero_return(error, bytes, errno)) {
        return ReadResult(bytes);
      }
      auto errError = ERR_get_error();
      VLOG(6) << "AsyncSSLSocket(fd=" << fd_ << ", "
              << "state=" << state_ << ", "
              << "sslState=" << sslState_ << ", "
              << "events=" << std::hex << eventFlags_ << "): "
              << "bytes: " << bytes << ", "
              << "error: " << error << ", "
              << "errno: " << errno << ", "
              << "func: " << ERR_func_error_string(errError) << ", "
              << "reason: " << ERR_reason_error_string(errError);
      return ReadResult(
          READ_ERROR,
          std::make_unique<SSLException>(error, errError, bytes, errno));
    }
  } else {
    appBytesReceived_ += bytes;
    return ReadResult(bytes);
  }
}

void AsyncSSLSocket::handleWrite() noexcept {
  VLOG(5) << "AsyncSSLSocket::handleWrite() this=" << this << ", fd=" << fd_
          << ", state=" << int(state_) << ", "
          << "sslState=" << sslState_ << ", events=" << eventFlags_;
  if (state_ < StateEnum::ESTABLISHED) {
    return AsyncSocket::handleWrite();
  }

  if (sslState_ == STATE_ACCEPTING) {
    assert(server_);
    handleAccept();
    return;
  }

  if (sslState_ == STATE_CONNECTING) {
    assert(!server_);
    handleConnect();
    return;
  }

  // Normal write
  AsyncSocket::handleWrite();
}

AsyncSocket::WriteResult AsyncSSLSocket::interpretSSLError(int rc, int error) {
  if (error == SSL_ERROR_WANT_READ) {
    // Even though we are attempting to write data, SSL_write() may
    // need to read data if renegotiation is being performed.  We currently
    // don't support this and just fail the write.
    LOG(ERROR) << "AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
               << ", sslState=" << sslState_ << ", events=" << eventFlags_
               << "): "
               << "unsupported SSL renegotiation during write";
    return WriteResult(
        WRITE_ERROR,
        std::make_unique<SSLException>(SSLError::INVALID_RENEGOTIATION));
  } else {
    auto errError = ERR_get_error();
    VLOG(3) << "ERROR: AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
            << ", sslState=" << sslState_ << ", events=" << eventFlags_ << "): "
            << "SSL error: " << error << ", errno: " << errno
            << ", func: " << ERR_func_error_string(errError)
            << ", reason: " << ERR_reason_error_string(errError);
    return WriteResult(
        WRITE_ERROR,
        std::make_unique<SSLException>(error, errError, rc, errno));
  }
}

AsyncSocket::WriteResult AsyncSSLSocket::performWrite(
    const iovec* vec,
    uint32_t count,
    WriteFlags flags,
    uint32_t* countWritten,
    uint32_t* partialWritten) {
  if (sslState_ == STATE_UNENCRYPTED) {
    return AsyncSocket::performWrite(
        vec, count, flags, countWritten, partialWritten);
  }
  if (sslState_ != STATE_ESTABLISHED) {
    LOG(ERROR) << "AsyncSSLSocket(fd=" << fd_ << ", state=" << int(state_)
               << ", sslState=" << sslState_ << ", events=" << eventFlags_
               << "): "
               << "TODO: AsyncSSLSocket currently does not support calling "
               << "write() before the handshake has fully completed";
    return WriteResult(
        WRITE_ERROR, std::make_unique<SSLException>(SSLError::EARLY_WRITE));
  }

  // Declare a buffer used to hold small write requests.  It could point to a
  // memory block either on stack or on heap. If it is on heap, we release it
  // manually when scope exits
  char* combinedBuf{nullptr};
  SCOPE_EXIT {
    // Note, always keep this check consistent with what we do below
    if (combinedBuf != nullptr && minWriteSize_ > MAX_STACK_BUF_SIZE) {
      delete[] combinedBuf;
    }
  };

  *countWritten = 0;
  *partialWritten = 0;
  ssize_t totalWritten = 0;
  size_t bytesStolenFromNextBuffer = 0;
  for (uint32_t i = 0; i < count; i++) {
    const iovec* v = vec + i;
    size_t offset = bytesStolenFromNextBuffer;
    bytesStolenFromNextBuffer = 0;
    size_t len = v->iov_len - offset;
    const void* buf;
    if (len == 0) {
      (*countWritten)++;
      continue;
    }
    buf = ((const char*)v->iov_base) + offset;

    ssize_t bytes;
    uint32_t buffersStolen = 0;
    auto sslWriteBuf = buf;
    if ((len < minWriteSize_) && ((i + 1) < count)) {
      // Combine this buffer with part or all of the next buffers in
      // order to avoid really small-grained calls to SSL_write().
      // Each call to SSL_write() produces a separate record in
      // the egress SSL stream, and we've found that some low-end
      // mobile clients can't handle receiving an HTTP response
      // header and the first part of the response body in two
      // separate SSL records (even if those two records are in
      // the same TCP packet).

      if (combinedBuf == nullptr) {
        if (minWriteSize_ > MAX_STACK_BUF_SIZE) {
          // Allocate the buffer on heap
          combinedBuf = new char[minWriteSize_];
        } else {
          // Allocate the buffer on stack
          combinedBuf = (char*)alloca(minWriteSize_);
        }
      }
      assert(combinedBuf != nullptr);
      sslWriteBuf = combinedBuf;

      memcpy(combinedBuf, buf, len);
      do {
        // INVARIANT: i + buffersStolen == complete chunks serialized
        uint32_t nextIndex = i + buffersStolen + 1;
        bytesStolenFromNextBuffer =
            std::min(vec[nextIndex].iov_len, minWriteSize_ - len);
        if (bytesStolenFromNextBuffer > 0) {
          assert(vec[nextIndex].iov_base != nullptr);
          ::memcpy(
              combinedBuf + len,
              vec[nextIndex].iov_base,
              bytesStolenFromNextBuffer);
        }
        len += bytesStolenFromNextBuffer;
        if (bytesStolenFromNextBuffer < vec[nextIndex].iov_len) {
          // couldn't steal the whole buffer
          break;
        } else {
          bytesStolenFromNextBuffer = 0;
          buffersStolen++;
        }
      } while ((i + buffersStolen + 1) < count && (len < minWriteSize_));
    }

    // Advance any empty buffers immediately after.
    if (bytesStolenFromNextBuffer == 0) {
      while ((i + buffersStolen + 1) < count &&
             vec[i + buffersStolen + 1].iov_len == 0) {
        buffersStolen++;
      }
    }

    // From here, the write flow is as follows:
    //   - sslWriteImpl calls SSL_write, which encrypts the passed buffer.
    //   - SSL_write calls AsyncSSLSocket::bioWrite with the encrypted buffer.
    //   - AsyncSSLSocket::bioWrite calls AsyncSocket::sendSocketMessage(...).
    //
    // When sendSocketMessage calls sendMsg, WriteFlags are transformed into
    // ancillary data and/or sendMsg flags. If WriteFlag::EOR is in flags and
    // trackEor_ is set, then we should ensure that MSG_EOR is only passed to
    // sendmsg when the final byte of the orginally passed in buffer is being
    // written. Since the buffer originally passed to performWrite may be split
    // up and written over multiple calls to sendmsg, we have to take care to
    // unset the EOR flag if it was included in the WriteFlags passed in and
    // we're writing a buffer that does _not_ contain the final byte of the
    // orignally passed buffer.
    //
    // We handle EOR as follows:
    //   - We set currWriteFlags_ to the passed in WriteFlags.
    //   - If sslWriteBuf does NOT contain the last byte of the passed in iovec,
    //     then we set currBytesToFinalByte_ to folly::none. In bioWrite, we
    //     unset WriteFlags::EOR if it is set in currWriteFlags_.
    //   - If sslWriteBuf DOES contain the last byte of the passed in iovec,
    //     then we set bytesToFinalByte_ to int(len). In bioWrite, if the length
    //     of the passed in buffer >= currBytesToFinalByte_, then we leave the
    //     flags in currWriteFlags_ alone.
    //
    // What about timestamp flags?
    //   - We don't do any special handling for timestamping flags.
    //   - This may mean that more timestamps than necessary get generated, but
    //     that's OK; you already have to deal with that for timestamping due to
    //     the possibility of partial writes.
    //   - MSG_EOR used to be used for timestamping, but hasn't been for years.
    //
    // Finally, why even care about MSG_EOR, if not for timestamping?
    //   - If set, it is marked in the corresponding tcp_skb_cb; this can be
    //     useful when debugging.
    //   - The kernel uses it to decide whether socket buffers can be collapsed
    //     together (see tcp_skb_can_collapse_to).
    currWriteFlags_ = flags;
    uint32_t iovecWrittenToSslWriteBuf = i + buffersStolen + 1;
    CHECK_LE(iovecWrittenToSslWriteBuf, count);
    if (iovecWrittenToSslWriteBuf == count) { // last byte is in sslWriteBuf
      currBytesToFinalByte_ = len; // length of current buffer
    } else { // there are still remaining buffers / iovec to write
      currBytesToFinalByte_ = folly::none;
      currWriteFlags_ |= WriteFlags::CORK;
    }

    bytes = sslWriteImpl(ssl_.get(), sslWriteBuf, int(len));
    if (bytes <= 0) {
      int error = sslGetErrorImpl(ssl_.get(), int(bytes));
      if (error == SSL_ERROR_WANT_WRITE) {
        // The entire buffer needs to be passed in again, so *partialWritten
        // is set to the original offset where we started for this call to
        // performWrite(); see SSL_ERROR_WANT_WRITE documentation for details.
        //
        // The caller will register for write event if not already.
        *partialWritten = uint32_t(offset);
        return WriteResult(totalWritten);
      }
      return interpretSSLError(int(bytes), error);
    }

    totalWritten += bytes;
    appBytesWritten_ += bytes;

    if (bytes == (ssize_t)len) {
      // The full iovec is written.
      (*countWritten) += 1 + buffersStolen;
      i += buffersStolen;
      // continue
    } else {
      bytes += offset; // adjust bytes to account for all of v
      while (bytes >= (ssize_t)v->iov_len) {
        // We combined this buf with part or all of the next one, and
        // we managed to write all of this buf but not all of the bytes
        // from the next one that we'd hoped to write.
        bytes -= v->iov_len;
        (*countWritten)++;
        v = &(vec[++i]);
      }
      *partialWritten = uint32_t(bytes);
      return WriteResult(totalWritten);
    }
  }

  return WriteResult(totalWritten);
}

void AsyncSSLSocket::sslInfoCallback(const SSL* ssl, int where, int ret) {
  AsyncSSLSocket* sslSocket = AsyncSSLSocket::getFromSSL(ssl);
  if (sslSocket->handshakeComplete_ && (where & SSL_CB_HANDSHAKE_START)) {
    sslSocket->renegotiateAttempted_ = true;
  }
  if (sslSocket->handshakeComplete_ && (where & SSL_CB_WRITE_ALERT)) {
    const char* desc = SSL_alert_desc_string(ret);
    if (desc && strcmp(desc, "NR") == 0) {
      sslSocket->renegotiateAttempted_ = true;
    }
  }
  if (where & SSL_CB_READ_ALERT) {
    const char* type = SSL_alert_type_string(ret);
    if (type) {
      const char* desc = SSL_alert_desc_string(ret);
      sslSocket->alertsReceived_.emplace_back(
          *type, StringPiece(desc, std::strlen(desc)));
    }
  }
}

int AsyncSSLSocket::bioWrite(BIO* b, const char* in, int inl) {
  // get pointer to AsyncSSLSocket from BioAppData
  auto appData = OpenSSLUtils::getBioAppData(b);
  CHECK(appData);
  AsyncSSLSocket* sslSock = reinterpret_cast<AsyncSSLSocket*>(appData);
  CHECK(sslSock);

  // if EOR is tracked, correct if needed
  WriteFlags flags = sslSock->currWriteFlags_;
  if (sslSock->trackEor_ &&
      (!sslSock->currBytesToFinalByte_.has_value() ||
       *(sslSock->currBytesToFinalByte_) > (size_t)inl)) {
    // unset EOR if set, since we're not writing the last byte yet
    flags = unSet(flags, folly::WriteFlags::EOR);
  }

  struct msghdr msg;
  struct iovec iov;
  iov.iov_base = const_cast<char*>(in);
  iov.iov_len = size_t(inl);
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  int msg_flags =
      sslSock->getSendMsgParamsCB()->getFlags(flags, false /*zeroCopyEnabled*/);
  msg.msg_controllen =
      sslSock->getSendMsgParamsCB()->getAncillaryDataSize(flags);
  CHECK_GE(
      AsyncSocket::SendMsgParamsCallback::maxAncillaryDataSize,
      msg.msg_controllen);
  if (msg.msg_controllen != 0) {
    msg.msg_control = reinterpret_cast<char*>(alloca(msg.msg_controllen));
    sslSock->getSendMsgParamsCB()->getAncillaryData(flags, msg.msg_control);
  }

  auto result =
      sslSock->sendSocketMessage(OpenSSLUtils::getBioFd(b), &msg, msg_flags);
  BIO_clear_retry_flags(b);
  if (!result.exception && result.writeReturn <= 0) {
    if (OpenSSLUtils::getBioShouldRetryWrite(int(result.writeReturn))) {
      BIO_set_retry_write(b);
    }
  }
  return int(result.writeReturn);
}

int AsyncSSLSocket::bioRead(BIO* b, char* out, int outl) {
  if (!out) {
    return 0;
  }
  BIO_clear_retry_flags(b);

  auto appData = OpenSSLUtils::getBioAppData(b);
  CHECK(appData);
  auto sslSock = reinterpret_cast<AsyncSSLSocket*>(appData);

  if (sslSock->preReceivedData_ && !sslSock->preReceivedData_->empty()) {
    VLOG(5) << "AsyncSSLSocket::bioRead() this=" << sslSock
            << ", reading pre-received data";

    Cursor cursor(sslSock->preReceivedData_.get());
    auto len = cursor.pullAtMost(out, outl);

    IOBufQueue queue;
    queue.append(std::move(sslSock->preReceivedData_));
    queue.trimStart(len);
    sslSock->preReceivedData_ = queue.move();
    return static_cast<int>(len);
  } else {
    auto result = int(netops::recv(OpenSSLUtils::getBioFd(b), out, outl, 0));
    if (result <= 0 && OpenSSLUtils::getBioShouldRetryWrite(result)) {
      BIO_set_retry_read(b);
    }
    return result;
  }
}

int AsyncSSLSocket::sslVerifyCallback(
    int preverifyOk,
    X509_STORE_CTX* x509Ctx) {
  SSL* ssl = (SSL*)X509_STORE_CTX_get_ex_data(
      x509Ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
  AsyncSSLSocket* self = AsyncSSLSocket::getFromSSL(ssl);

  VLOG(3) << "AsyncSSLSocket::sslVerifyCallback() this=" << self << ", "
          << "fd=" << self->fd_ << ", preverifyOk=" << preverifyOk;

  return (self->handshakeCallback_)
      ? self->handshakeCallback_->handshakeVer(self, preverifyOk, x509Ctx)
      : preverifyOk;
}

void AsyncSSLSocket::enableClientHelloParsing() {
  parseClientHello_ = true;
  clientHelloInfo_ = std::make_unique<ssl::ClientHelloInfo>();
}

void AsyncSSLSocket::resetClientHelloParsing(SSL* ssl) {
  SSL_set_msg_callback(ssl, nullptr);
  SSL_set_msg_callback_arg(ssl, nullptr);
  clientHelloInfo_->clientHelloBuf_.clear();
}

void AsyncSSLSocket::clientHelloParsingCallback(
    int written,
    int /* version */,
    int contentType,
    const void* buf,
    size_t len,
    SSL* ssl,
    void* arg) {
  auto sock = static_cast<AsyncSSLSocket*>(arg);
  if (written != 0) {
    sock->resetClientHelloParsing(ssl);
    return;
  }
  if (contentType != SSL3_RT_HANDSHAKE) {
    return;
  }
  if (len == 0) {
    return;
  }

  auto& clientHelloBuf = sock->clientHelloInfo_->clientHelloBuf_;
  clientHelloBuf.append(IOBuf::wrapBuffer(buf, len));
  try {
    Cursor cursor(clientHelloBuf.front());
    if (cursor.read<uint8_t>() != SSL3_MT_CLIENT_HELLO) {
      sock->resetClientHelloParsing(ssl);
      return;
    }

    if (cursor.totalLength() < 3) {
      clientHelloBuf.trimEnd(len);
      clientHelloBuf.append(IOBuf::copyBuffer(buf, len));
      return;
    }

    uint32_t messageLength = cursor.read<uint8_t>();
    messageLength <<= 8;
    messageLength |= cursor.read<uint8_t>();
    messageLength <<= 8;
    messageLength |= cursor.read<uint8_t>();
    if (cursor.totalLength() < messageLength) {
      clientHelloBuf.trimEnd(len);
      clientHelloBuf.append(IOBuf::copyBuffer(buf, len));
      return;
    }

    sock->clientHelloInfo_->clientHelloMajorVersion_ = cursor.read<uint8_t>();
    sock->clientHelloInfo_->clientHelloMinorVersion_ = cursor.read<uint8_t>();

    cursor.skip(4); // gmt_unix_time
    cursor.skip(28); // random_bytes

    cursor.skip(cursor.read<uint8_t>()); // session_id

    auto cipherSuitesLength = cursor.readBE<uint16_t>();
    for (int i = 0; i < cipherSuitesLength; i += 2) {
      sock->clientHelloInfo_->clientHelloCipherSuites_.push_back(
          cursor.readBE<uint16_t>());
    }

    auto compressionMethodsLength = cursor.read<uint8_t>();
    for (int i = 0; i < compressionMethodsLength; ++i) {
      sock->clientHelloInfo_->clientHelloCompressionMethods_.push_back(
          cursor.readBE<uint8_t>());
    }

    if (cursor.totalLength() > 0) {
      auto extensionsLength = cursor.readBE<uint16_t>();
      while (extensionsLength) {
        auto extensionType =
            static_cast<ssl::TLSExtension>(cursor.readBE<uint16_t>());
        sock->clientHelloInfo_->clientHelloExtensions_.push_back(extensionType);
        extensionsLength -= 2;
        auto extensionDataLength = cursor.readBE<uint16_t>();
        extensionsLength -= 2;
        extensionsLength -= extensionDataLength;

        if (extensionType == ssl::TLSExtension::SIGNATURE_ALGORITHMS) {
          cursor.skip(2);
          extensionDataLength -= 2;
          while (extensionDataLength) {
            auto hashAlg =
                static_cast<ssl::HashAlgorithm>(cursor.readBE<uint8_t>());
            auto sigAlg =
                static_cast<ssl::SignatureAlgorithm>(cursor.readBE<uint8_t>());
            extensionDataLength -= 2;
            sock->clientHelloInfo_->clientHelloSigAlgs_.emplace_back(
                hashAlg, sigAlg);
          }
        } else if (extensionType == ssl::TLSExtension::SUPPORTED_VERSIONS) {
          cursor.skip(1);
          extensionDataLength -= 1;
          while (extensionDataLength) {
            sock->clientHelloInfo_->clientHelloSupportedVersions_.push_back(
                cursor.readBE<uint16_t>());
            extensionDataLength -= 2;
          }
        } else if (extensionType == ssl::TLSExtension::SERVER_NAME) {
          cursor.skip(2);
          extensionDataLength -= 2;
          while (extensionDataLength) {
            static_assert(
                std::is_same<
                    typename std::underlying_type<ssl::NameType>::type,
                    uint8_t>::value,
                "unexpected underlying type");

            auto typ = static_cast<ssl::NameType>(cursor.readBE<uint8_t>());
            auto nameLength = cursor.readBE<uint16_t>();

            if (typ == NameType::HOST_NAME &&
                sock->clientHelloInfo_->clientHelloSNIHostname_.empty() &&
                cursor.canAdvance(nameLength)) {
              sock->clientHelloInfo_->clientHelloSNIHostname_ =
                  cursor.readFixedString(nameLength);
            } else {
              // Must attempt to skip |nameLength| in order to keep cursor
              // in sync. If the remaining buffer length is smaller than
              // nameLength, this will throw.
              cursor.skip(nameLength);
            }
            extensionDataLength -=
                sizeof(typ) + sizeof(nameLength) + nameLength;
          }
        } else {
          cursor.skip(extensionDataLength);
        }
      }
    }
  } catch (std::out_of_range&) {
    // we'll use what we found and cleanup below.
    VLOG(4) << "AsyncSSLSocket::clientHelloParsingCallback(): "
            << "buffer finished unexpectedly."
            << " AsyncSSLSocket socket=" << sock;
  }

  sock->resetClientHelloParsing(ssl);
}

void AsyncSSLSocket::getSSLClientCiphers(
    std::string& clientCiphers,
    bool convertToString) const {
  std::string ciphers;

  if (!parseClientHello_ ||
      clientHelloInfo_->clientHelloCipherSuites_.empty()) {
    clientCiphers = "";
    return;
  }

  bool first = true;
  for (auto originalCipherCode : clientHelloInfo_->clientHelloCipherSuites_) {
    if (first) {
      first = false;
    } else {
      ciphers += ":";
    }

    bool nameFound = convertToString;

    if (convertToString) {
      const auto& name = OpenSSLUtils::getCipherName(originalCipherCode);
      if (name.empty()) {
        nameFound = false;
      } else {
        ciphers += name;
      }
    }

    if (!nameFound) {
      folly::hexlify(
          std::array<uint8_t, 2>{
              {static_cast<uint8_t>((originalCipherCode >> 8) & 0xffL),
               static_cast<uint8_t>(originalCipherCode & 0x00ffL)}},
          ciphers,
          /* append to ciphers = */ true);
    }
  }

  clientCiphers = std::move(ciphers);
}

std::string AsyncSSLSocket::getSSLClientComprMethods() const {
  if (!parseClientHello_) {
    return "";
  }
  return folly::join(":", clientHelloInfo_->clientHelloCompressionMethods_);
}

std::string AsyncSSLSocket::getSSLClientExts() const {
  if (!parseClientHello_) {
    return "";
  }
  return folly::join(":", clientHelloInfo_->clientHelloExtensions_);
}

std::string AsyncSSLSocket::getSSLClientSigAlgs() const {
  if (!parseClientHello_) {
    return "";
  }

  std::string sigAlgs;
  sigAlgs.reserve(clientHelloInfo_->clientHelloSigAlgs_.size() * 4);
  for (size_t i = 0; i < clientHelloInfo_->clientHelloSigAlgs_.size(); i++) {
    if (i) {
      sigAlgs.push_back(':');
    }
    sigAlgs.append(
        folly::to<std::string>(clientHelloInfo_->clientHelloSigAlgs_[i].first));
    sigAlgs.push_back(',');
    sigAlgs.append(folly::to<std::string>(
        clientHelloInfo_->clientHelloSigAlgs_[i].second));
  }

  return sigAlgs;
}

std::string AsyncSSLSocket::getSSLClientSupportedVersions() const {
  if (!parseClientHello_) {
    return "";
  }
  return folly::join(":", clientHelloInfo_->clientHelloSupportedVersions_);
}

std::string AsyncSSLSocket::getSSLAlertsReceived() const {
  std::string ret;

  for (const auto& alert : alertsReceived_) {
    if (!ret.empty()) {
      ret.append(",");
    }
    ret.append(folly::to<std::string>(alert.first, ": ", alert.second));
  }

  return ret;
}

void AsyncSSLSocket::setSSLCertVerificationAlert(std::string alert) {
  sslVerificationAlert_ = std::move(alert);
}

std::string AsyncSSLSocket::getSSLCertVerificationAlert() const {
  return sslVerificationAlert_;
}

void AsyncSSLSocket::getSSLSharedCiphers(std::string& sharedCiphers) const {
  char ciphersBuffer[1024];
  ciphersBuffer[0] = '\0';
  SSL_get_shared_ciphers(ssl_.get(), ciphersBuffer, sizeof(ciphersBuffer) - 1);
  sharedCiphers = ciphersBuffer;
}

void AsyncSSLSocket::getSSLServerCiphers(std::string& serverCiphers) const {
  serverCiphers = SSL_get_cipher_list(ssl_.get(), 0);
  int i = 1;
  const char* cipher;
  while ((cipher = SSL_get_cipher_list(ssl_.get(), i)) != nullptr) {
    serverCiphers.append(":");
    serverCiphers.append(cipher);
    i++;
  }
}

} // namespace folly
