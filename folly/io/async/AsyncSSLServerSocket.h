/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#pragma once

#include <folly/io/async/SSLContext.h>
#include <folly/io/async/AsyncServerSocket.h>


namespace folly {
class SocketAddress;
class AsyncSSLSocket;

class AsyncSSLServerSocket : public DelayedDestruction,
                             private AsyncServerSocket::AcceptCallback {
 public:
  class SSLAcceptCallback {
   public:
    virtual ~SSLAcceptCallback() {}

    /**
     * connectionAccepted() is called whenever a new client connection is
     * received.
     *
     * The SSLAcceptCallback will remain installed after connectionAccepted()
     * returns.
     *
     * @param sock        The newly accepted client socket.  The
     *                    SSLAcceptCallback
     *                    assumes ownership of this socket, and is responsible
     *                    for closing it when done.
     */
    virtual void connectionAccepted(
      const std::shared_ptr<AsyncSSLSocket> &sock)
      noexcept = 0;

    /**
     * acceptError() is called if an error occurs while accepting.
     *
     * The SSLAcceptCallback will remain installed even after an accept error.
     * If the callback wants to uninstall itself and stop trying to accept new
     * connections, it must explicit call setAcceptCallback(nullptr).
     *
     * @param ex  An exception representing the error.
     */
    virtual void acceptError(const std::exception& ex) noexcept = 0;
  };

  /**
   * Create a new TAsyncSSLServerSocket with the specified EventBase.
   *
   * @param eventBase  The EventBase to use for driving the asynchronous I/O.
   *                   If this parameter is nullptr, attachEventBase() must be
   *                   called before this socket can begin accepting
   *                   connections.  All TAsyncSSLSocket objects accepted by
   *                   this server socket will be attached to this EventBase
   *                   when they are created.
   */
  explicit AsyncSSLServerSocket(
    const std::shared_ptr<folly::SSLContext>& ctx,
    EventBase* eventBase = nullptr);

  /**
   * Destroy the socket.
   *
   * destroy() must be called to destroy the socket.  The normal destructor is
   * private, and should not be invoked directly.  This prevents callers from
   * deleting a TAsyncSSLServerSocket while it is invoking a callback.
   */
  virtual void destroy();

  virtual void bind(const folly::SocketAddress& address) {
    serverSocket_->bind(address);
  }
  virtual void bind(uint16_t port) {
    serverSocket_->bind(port);
  }
  void getAddress(folly::SocketAddress* addressReturn) {
    serverSocket_->getAddress(addressReturn);
  }
  virtual void listen(int backlog) {
    serverSocket_->listen(backlog);
  }

  /**
   * Helper function to create a shared_ptr<TAsyncSSLServerSocket>.
   *
   * This passes in the correct destructor object, since TAsyncSSLServerSocket's
   * destructor is protected and cannot be invoked directly.
   */
  static std::shared_ptr<AsyncSSLServerSocket> newSocket(
    const std::shared_ptr<folly::SSLContext>& ctx,
        EventBase* evb) {
    return std::shared_ptr<AsyncSSLServerSocket>(
      new AsyncSSLServerSocket(ctx, evb),
      Destructor());
  }

  /**
   * Set the accept callback.
   *
   * This method may only be invoked from the EventBase's loop thread.
   *
   * @param callback The callback to invoke when a new socket
   *                 connection is accepted and a new TAsyncSSLSocket is
   *                 created.
   *
   * Throws TTransportException on error.
   */
  void setSSLAcceptCallback(SSLAcceptCallback* callback);

  SSLAcceptCallback *getSSLAcceptCallback() const {
    return sslCallback_;
  }

  void attachEventBase(EventBase* eventBase);
  void detachEventBase();

  /**
   * Returns the EventBase that the handler is currently attached to.
   */
  EventBase* getEventBase() const {
    return eventBase_;
  }

 protected:
  /**
   * Protected destructor.
   *
   * Invoke destroy() instead to destroy the TAsyncSSLServerSocket.
   */
  virtual ~AsyncSSLServerSocket();

 protected:
  virtual void connectionAccepted(int fd,
                                  const folly::SocketAddress& clientAddr)
    noexcept;
  virtual void acceptError(const std::exception& ex) noexcept;

  EventBase* eventBase_;
  AsyncServerSocket* serverSocket_;
  // SSL context
  std::shared_ptr<folly::SSLContext> ctx_;
  // The accept callback
  SSLAcceptCallback* sslCallback_;
};

} // namespace
