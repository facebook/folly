/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/acceptor/Acceptor.h>

#include <folly/wangle/acceptor/ManagedConnection.h>
#include <folly/wangle/ssl/SSLContextManager.h>

#include <boost/cast.hpp>
#include <fcntl.h>
#include <folly/ScopeGuard.h>
#include <folly/io/async/EventBase.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <gflags/gflags.h>
#include <unistd.h>

using folly::wangle::ConnectionManager;
using folly::wangle::ManagedConnection;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::filebuf;
using std::ifstream;
using std::ios;
using std::shared_ptr;
using std::string;

namespace folly {

#ifndef NO_LIB_GFLAGS
DEFINE_int32(shutdown_idle_grace_ms, 5000, "milliseconds to wait before "
             "closing idle conns");
#else
const int32_t FLAGS_shutdown_idle_grace_ms = 5000;
#endif

static const std::string empty_string;
std::atomic<uint64_t> Acceptor::totalNumPendingSSLConns_{0};

/**
 * Lightweight wrapper class to keep track of a newly
 * accepted connection during SSL handshaking.
 */
class AcceptorHandshakeHelper :
      public AsyncSSLSocket::HandshakeCB,
      public ManagedConnection {
 public:
  AcceptorHandshakeHelper(AsyncSSLSocket::UniquePtr socket,
                          Acceptor* acceptor,
                          const SocketAddress& clientAddr,
                          std::chrono::steady_clock::time_point acceptTime,
                          TransportInfo& tinfo)
    : socket_(std::move(socket)), acceptor_(acceptor),
      acceptTime_(acceptTime), clientAddr_(clientAddr),
      tinfo_(tinfo) {
    acceptor_->downstreamConnectionManager_->addConnection(this, true);
    if(acceptor_->parseClientHello_)  {
      socket_->enableClientHelloParsing();
    }
    socket_->sslAccept(this);
  }

  virtual void timeoutExpired() noexcept {
    VLOG(4) << "SSL handshake timeout expired";
    sslError_ = SSLErrorEnum::TIMEOUT;
    dropConnection();
  }
  virtual void describe(std::ostream& os) const {
    os << "pending handshake on " << clientAddr_;
  }
  virtual bool isBusy() const {
    return true;
  }
  virtual void notifyPendingShutdown() {}
  virtual void closeWhenIdle() {}

  virtual void dropConnection() {
    VLOG(10) << "Dropping in progress handshake for " << clientAddr_;
    socket_->closeNow();
  }
  virtual void dumpConnectionState(uint8_t loglevel) {
  }

 private:
  // AsyncSSLSocket::HandshakeCallback API
  virtual void handshakeSuc(AsyncSSLSocket* sock) noexcept {

    const unsigned char* nextProto = nullptr;
    unsigned nextProtoLength = 0;
    sock->getSelectedNextProtocol(&nextProto, &nextProtoLength);
    if (VLOG_IS_ON(3)) {
      if (nextProto) {
        VLOG(3) << "Client selected next protocol " <<
            string((const char*)nextProto, nextProtoLength);
      } else {
        VLOG(3) << "Client did not select a next protocol";
      }
    }

    // fill in SSL-related fields from TransportInfo
    // the other fields like RTT are filled in the Acceptor
    tinfo_.ssl = true;
    tinfo_.acceptTime = acceptTime_;
    tinfo_.sslSetupTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - acceptTime_
    );
    tinfo_.sslSetupBytesRead = sock->getRawBytesReceived();
    tinfo_.sslSetupBytesWritten = sock->getRawBytesWritten();
    tinfo_.sslServerName = sock->getSSLServerName() ?
      std::make_shared<std::string>(sock->getSSLServerName()) : nullptr;
    tinfo_.sslCipher = sock->getNegotiatedCipherName() ?
      std::make_shared<std::string>(sock->getNegotiatedCipherName()) : nullptr;
    tinfo_.sslVersion = sock->getSSLVersion();
    tinfo_.sslCertSize = sock->getSSLCertSize();
    tinfo_.sslResume = SSLUtil::getResumeState(sock);
    tinfo_.sslClientCiphers = std::make_shared<std::string>();
    sock->getSSLClientCiphers(*tinfo_.sslClientCiphers);
    tinfo_.sslServerCiphers = std::make_shared<std::string>();
    sock->getSSLServerCiphers(*tinfo_.sslServerCiphers);
    tinfo_.sslClientComprMethods =
        std::make_shared<std::string>(sock->getSSLClientComprMethods());
    tinfo_.sslClientExts =
        std::make_shared<std::string>(sock->getSSLClientExts());
    tinfo_.sslNextProtocol = std::make_shared<std::string>();
    tinfo_.sslNextProtocol->assign(reinterpret_cast<const char*>(nextProto),
                                  nextProtoLength);

    acceptor_->updateSSLStats(
      sock,
      tinfo_.sslSetupTime,
      SSLErrorEnum::NO_ERROR
    );
    acceptor_->downstreamConnectionManager_->removeConnection(this);
    acceptor_->sslConnectionReady(std::move(socket_), clientAddr_,
        nextProto ? string((const char*)nextProto, nextProtoLength) :
                                  empty_string, tinfo_);
    delete this;
  }

  virtual void handshakeErr(AsyncSSLSocket* sock,
                            const AsyncSocketException& ex) noexcept {
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - acceptTime_);
    VLOG(3) << "SSL handshake error after " << elapsedTime.count() <<
        " ms; " << sock->getRawBytesReceived() << " bytes received & " <<
        sock->getRawBytesWritten() << " bytes sent: " <<
        ex.what();
    acceptor_->updateSSLStats(sock, elapsedTime, sslError_);
    acceptor_->sslConnectionError();
    delete this;
  }

  AsyncSSLSocket::UniquePtr socket_;
  Acceptor* acceptor_;
  std::chrono::steady_clock::time_point acceptTime_;
  SocketAddress clientAddr_;
  TransportInfo tinfo_;
  SSLErrorEnum sslError_{SSLErrorEnum::NO_ERROR};
};

Acceptor::Acceptor(const ServerSocketConfig& accConfig) :
  accConfig_(accConfig),
  socketOptions_(accConfig.getSocketOptions()) {
}

void
Acceptor::init(AsyncServerSocket* serverSocket,
               EventBase* eventBase) {
  CHECK(nullptr == this->base_);

  if (accConfig_.isSSL()) {
    if (!sslCtxManager_) {
      sslCtxManager_ = folly::make_unique<SSLContextManager>(
        eventBase,
        "vip_" + getName(),
        accConfig_.strictSSL, nullptr);
    }
    for (const auto& sslCtxConfig : accConfig_.sslContextConfigs) {
      sslCtxManager_->addSSLContextConfig(
        sslCtxConfig,
        accConfig_.sslCacheOptions,
        &accConfig_.initialTicketSeeds,
        accConfig_.bindAddress,
        cacheProvider_);
      parseClientHello_ |= sslCtxConfig.clientHelloParsingEnabled;
    }

    CHECK(sslCtxManager_->getDefaultSSLCtx());
  }

  base_ = eventBase;
  state_ = State::kRunning;
  downstreamConnectionManager_ = ConnectionManager::makeUnique(
    eventBase, accConfig_.connectionIdleTimeout, this);

  if (serverSocket) {
    serverSocket->addAcceptCallback(this, eventBase);

    for (auto& fd : serverSocket->getSockets()) {
      if (fd < 0) {
        continue;
      }
      for (const auto& opt: socketOptions_) {
        opt.first.apply(fd, opt.second);
      }
    }
  }
}

Acceptor::~Acceptor(void) {
}

void Acceptor::addSSLContextConfig(const SSLContextConfig& sslCtxConfig) {
  sslCtxManager_->addSSLContextConfig(sslCtxConfig,
                                      accConfig_.sslCacheOptions,
                                      &accConfig_.initialTicketSeeds,
                                      accConfig_.bindAddress,
                                      cacheProvider_);
}

void
Acceptor::drainAllConnections() {
  if (downstreamConnectionManager_) {
    downstreamConnectionManager_->initiateGracefulShutdown(
      std::chrono::milliseconds(FLAGS_shutdown_idle_grace_ms));
  }
}

void Acceptor::setLoadShedConfig(const LoadShedConfiguration& from,
                       IConnectionCounter* counter) {
  loadShedConfig_ = from;
  connectionCounter_ = counter;
}

bool Acceptor::canAccept(const SocketAddress& address) {
  if (!connectionCounter_) {
    return true;
  }

  uint64_t maxConnections = connectionCounter_->getMaxConnections();
  if (maxConnections == 0) {
    return true;
  }

  uint64_t currentConnections = connectionCounter_->getNumConnections();
  if (currentConnections < maxConnections) {
    return true;
  }

  if (loadShedConfig_.isWhitelisted(address)) {
    return true;
  }

  // Take care of comparing connection count against max connections across
  // all acceptors. Expensive since a lock must be taken to get the counter.
  auto connectionCountForLoadShedding = getConnectionCountForLoadShedding();
  if (connectionCountForLoadShedding < loadShedConfig_.getMaxConnections()) {
    return true;
  }

  VLOG(4) << address.describe() << " not whitelisted";
  return false;
}

void
Acceptor::connectionAccepted(
    int fd, const SocketAddress& clientAddr) noexcept {
  if (!canAccept(clientAddr)) {
    close(fd);
    return;
  }
  auto acceptTime = std::chrono::steady_clock::now();
  for (const auto& opt: socketOptions_) {
    opt.first.apply(fd, opt.second);
  }

  onDoneAcceptingConnection(fd, clientAddr, acceptTime);
}

void Acceptor::onDoneAcceptingConnection(
    int fd,
    const SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime) noexcept {
  TransportInfo tinfo;
  processEstablishedConnection(fd, clientAddr, acceptTime, tinfo);
}

void
Acceptor::processEstablishedConnection(
    int fd,
    const SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo) noexcept {
  if (accConfig_.isSSL()) {
    CHECK(sslCtxManager_);
    AsyncSSLSocket::UniquePtr sslSock(
      makeNewAsyncSSLSocket(
        sslCtxManager_->getDefaultSSLCtx(), base_, fd));
    ++numPendingSSLConns_;
    ++totalNumPendingSSLConns_;
    if (totalNumPendingSSLConns_ > accConfig_.maxConcurrentSSLHandshakes) {
      VLOG(2) << "dropped SSL handshake on " << accConfig_.name <<
        " too many handshakes in progress";
      updateSSLStats(sslSock.get(), std::chrono::milliseconds(0),
                     SSLErrorEnum::DROPPED);
      sslConnectionError();
      return;
    }
    new AcceptorHandshakeHelper(
      std::move(sslSock),
      this,
      clientAddr,
      acceptTime,
      tinfo
    );
  } else {
    tinfo.ssl = false;
    tinfo.acceptTime = acceptTime;
    AsyncSocket::UniquePtr sock(makeNewAsyncSocket(base_, fd));
    connectionReady(std::move(sock), clientAddr, empty_string, tinfo);
  }
}

void
Acceptor::connectionReady(
    AsyncSocket::UniquePtr sock,
    const SocketAddress& clientAddr,
    const string& nextProtocolName,
    TransportInfo& tinfo) {
  // Limit the number of reads from the socket per poll loop iteration,
  // both to keep memory usage under control and to prevent one fast-
  // writing client from starving other connections.
  sock->setMaxReadsPerEvent(16);
  tinfo.initWithSocket(sock.get());
  onNewConnection(std::move(sock), &clientAddr, nextProtocolName, tinfo);
}

void
Acceptor::sslConnectionReady(AsyncSocket::UniquePtr sock,
                             const SocketAddress& clientAddr,
                             const string& nextProtocol,
                             TransportInfo& tinfo) {
  CHECK(numPendingSSLConns_ > 0);
  connectionReady(std::move(sock), clientAddr, nextProtocol, tinfo);
  --numPendingSSLConns_;
  --totalNumPendingSSLConns_;
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void
Acceptor::sslConnectionError() {
  CHECK(numPendingSSLConns_ > 0);
  --numPendingSSLConns_;
  --totalNumPendingSSLConns_;
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void
Acceptor::acceptError(const std::exception& ex) noexcept {
  // An error occurred.
  // The most likely error is out of FDs.  AsyncServerSocket will back off
  // briefly if we are out of FDs, then continue accepting later.
  // Just log a message here.
  LOG(ERROR) << "error accepting on acceptor socket: " << ex.what();
}

void
Acceptor::acceptStopped() noexcept {
  VLOG(3) << "Acceptor " << this << " acceptStopped()";
  // Drain the open client connections
  drainAllConnections();

  // If we haven't yet finished draining, begin doing so by marking ourselves
  // as in the draining state. We must be sure to hit checkDrained() here, as
  // if we're completely idle, we can should consider ourself drained
  // immediately (as there is no outstanding work to complete to cause us to
  // re-evaluate this).
  if (state_ != State::kDone) {
    state_ = State::kDraining;
    checkDrained();
  }
}

void
Acceptor::onEmpty(const ConnectionManager& cm) {
  VLOG(3) << "Acceptor=" << this << " onEmpty()";
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void
Acceptor::checkDrained() {
  CHECK(state_ == State::kDraining);
  if (forceShutdownInProgress_ ||
      (downstreamConnectionManager_->getNumConnections() != 0) ||
      (numPendingSSLConns_ != 0)) {
    return;
  }

  VLOG(2) << "All connections drained from Acceptor=" << this << " in thread "
          << base_;

  downstreamConnectionManager_.reset();

  state_ = State::kDone;

  onConnectionsDrained();
}

milliseconds
Acceptor::getConnTimeout() const {
  return accConfig_.connectionIdleTimeout;
}

void Acceptor::addConnection(ManagedConnection* conn) {
  // Add the socket to the timeout manager so that it can be cleaned
  // up after being left idle for a long time.
  downstreamConnectionManager_->addConnection(conn, true);
}

void
Acceptor::forceStop() {
  base_->runInEventBaseThread([&] { dropAllConnections(); });
}

void
Acceptor::dropAllConnections() {
  if (downstreamConnectionManager_) {
    VLOG(3) << "Dropping all connections from Acceptor=" << this <<
      " in thread " << base_;
    assert(base_->isInEventBaseThread());
    forceShutdownInProgress_ = true;
    downstreamConnectionManager_->dropAllConnections();
    CHECK(downstreamConnectionManager_->getNumConnections() == 0);
    downstreamConnectionManager_.reset();
  }
  CHECK(numPendingSSLConns_ == 0);

  state_ = State::kDone;
  onConnectionsDrained();
}

} // namespace
