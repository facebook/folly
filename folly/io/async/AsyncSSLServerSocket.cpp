/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/io/async/AsyncSSLServerSocket.h>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/SocketAddress.h>

using std::shared_ptr;

namespace folly {

AsyncSSLServerSocket::AsyncSSLServerSocket(
      const shared_ptr<SSLContext>& ctx,
      EventBase* eventBase)
    : eventBase_(eventBase)
    , serverSocket_(new AsyncServerSocket(eventBase))
    , ctx_(ctx)
    , sslCallback_(nullptr) {
}

AsyncSSLServerSocket::~AsyncSSLServerSocket() {
}

void AsyncSSLServerSocket::destroy() {
  // Stop accepting on the underlying socket as soon as destroy is called
  if (sslCallback_ != nullptr) {
    serverSocket_->pauseAccepting();
    serverSocket_->removeAcceptCallback(this, nullptr);
  }
  serverSocket_->destroy();
  serverSocket_ = nullptr;
  sslCallback_ = nullptr;

  DelayedDestruction::destroy();
}

void AsyncSSLServerSocket::setSSLAcceptCallback(SSLAcceptCallback* callback) {
  SSLAcceptCallback *oldCallback = sslCallback_;
  sslCallback_ = callback;
  if (callback != nullptr && oldCallback == nullptr) {
    serverSocket_->addAcceptCallback(this, nullptr);
    serverSocket_->startAccepting();
  } else if (callback == nullptr && oldCallback != nullptr) {
    serverSocket_->removeAcceptCallback(this, nullptr);
    serverSocket_->pauseAccepting();
  }
}

void AsyncSSLServerSocket::attachEventBase(EventBase* eventBase) {
  assert(sslCallback_ == nullptr);
  eventBase_ = eventBase;
  serverSocket_->attachEventBase(eventBase);
}

void AsyncSSLServerSocket::detachEventBase() {
  serverSocket_->detachEventBase();
  eventBase_ = nullptr;
}

void
AsyncSSLServerSocket::connectionAccepted(
  int fd,
  const folly::SocketAddress& clientAddr) noexcept {
  shared_ptr<AsyncSSLSocket> sslSock;
  try {
    // Create a AsyncSSLSocket object with the fd. The socket should be
    // added to the event base and in the state of accepting SSL connection.
    sslSock = AsyncSSLSocket::newSocket(ctx_, eventBase_, fd);
  } catch (const std::exception &e) {
    LOG(ERROR) << "Exception %s caught while creating a AsyncSSLSocket "
      "object with socket " << e.what() << fd;
    ::close(fd);
    sslCallback_->acceptError(e);
    return;
  }

  // TODO: Perform the SSL handshake before invoking the callback
  sslCallback_->connectionAccepted(sslSock);
}

void AsyncSSLServerSocket::acceptError(const std::exception& ex)
  noexcept {
  LOG(ERROR) << "AsyncSSLServerSocket accept error: " << ex.what();
  sslCallback_->acceptError(ex);
}

} // namespace
