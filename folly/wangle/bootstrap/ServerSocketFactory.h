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
#pragma once

#include <folly/wangle/bootstrap/ServerBootstrap-inl.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/AsyncUDPServerSocket.h>

namespace folly {

class ServerSocketFactory {
 public:
  virtual std::shared_ptr<AsyncSocketBase> newSocket(
    int port, SocketAddress address, int backlog,
    bool reuse, ServerSocketConfig& config) = 0;

  virtual void stopSocket(
    std::shared_ptr<AsyncSocketBase>& socket) = 0;

  virtual void removeAcceptCB(std::shared_ptr<AsyncSocketBase> sock, Acceptor *callback, EventBase* base) = 0;
  virtual void addAcceptCB(std::shared_ptr<AsyncSocketBase> sock, Acceptor* callback, EventBase* base) = 0 ;
  virtual ~ServerSocketFactory() = default;
};

class AsyncServerSocketFactory : public ServerSocketFactory {
 public:
  std::shared_ptr<AsyncSocketBase> newSocket(
      int port, SocketAddress address, int backlog, bool reuse,
      ServerSocketConfig& config) {

    auto socket = folly::AsyncServerSocket::newSocket();
    socket->setReusePortEnabled(reuse);
    socket->attachEventBase(EventBaseManager::get()->getEventBase());
    if (port >= 0) {
      socket->bind(port);
    } else {
      socket->bind(address);
    }

    socket->listen(config.acceptBacklog);
    socket->startAccepting();

    return socket;
  }

  virtual void stopSocket(
    std::shared_ptr<AsyncSocketBase>& s) {
    auto socket = std::dynamic_pointer_cast<AsyncServerSocket>(s);
    DCHECK(socket);
    socket->stopAccepting();
    socket->detachEventBase();
  }

  virtual void removeAcceptCB(std::shared_ptr<AsyncSocketBase> s,
                              Acceptor *callback, EventBase* base) {
    auto socket = std::dynamic_pointer_cast<AsyncServerSocket>(s);
    CHECK(socket);
    socket->removeAcceptCallback(callback, base);
  }

  virtual void addAcceptCB(std::shared_ptr<AsyncSocketBase> s,
                                 Acceptor* callback, EventBase* base) {
    auto socket = std::dynamic_pointer_cast<AsyncServerSocket>(s);
    CHECK(socket);
    socket->addAcceptCallback(callback, base);
  }
};

class AsyncUDPServerSocketFactory : public ServerSocketFactory {
 public:
  std::shared_ptr<AsyncSocketBase> newSocket(
      int port, SocketAddress address, int backlog, bool reuse,
      ServerSocketConfig& config) {

    auto socket = std::make_shared<AsyncUDPServerSocket>(
      EventBaseManager::get()->getEventBase());
    socket->setReusePort(reuse);
    if (port >= 0) {
      SocketAddress addressr("::1", port);
      socket->bind(addressr);
    } else {
      socket->bind(address);
    }
    socket->listen();

    return socket;
  }

  virtual void stopSocket(
    std::shared_ptr<AsyncSocketBase>& s) {
    auto socket = std::dynamic_pointer_cast<AsyncUDPServerSocket>(s);
    DCHECK(socket);
    socket->close();
  }

  virtual void removeAcceptCB(std::shared_ptr<AsyncSocketBase> s,
                              Acceptor *callback, EventBase* base) {
  }

  virtual void addAcceptCB(std::shared_ptr<AsyncSocketBase> s,
                                 Acceptor* callback, EventBase* base) {
    auto socket = std::dynamic_pointer_cast<AsyncUDPServerSocket>(s);
    DCHECK(socket);
    socket->addListener(base, callback);
  }
};

} // namespace
