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

#include <folly/Portability.h>

#include <folly/experimental/coro/Baton.h>
#include <folly/io/coro/ServerSocket.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

namespace {

class AcceptCallback : public folly::AsyncServerSocket::AcceptCallback {
 public:
  explicit AcceptCallback(
      Baton& baton, std::shared_ptr<folly::AsyncServerSocket> socket)
      : baton_{baton}, socket_(std::move(socket)) {}

  ~AcceptCallback() override = default;

  int acceptFd{-1};

  folly::exception_wrapper error;

 private:
  // to notify the caller of the result
  Baton& baton_;

  // the server socket
  std::shared_ptr<folly::AsyncServerSocket> socket_;

  //
  // AcceptCallback methods
  //

  void connectionAccepted(
      folly::NetworkSocket fdNetworkSocket,
      const folly::SocketAddress& clientAddr,
      AcceptInfo /* info */) noexcept override {
    VLOG(5) << "Connection accepted from: " << clientAddr.describe();
    // unregister handlers while in the callback
    socket_->pauseAccepting();
    socket_->removeAcceptCallback(this, nullptr);
    acceptFd = fdNetworkSocket.toFd();
    baton_.post();
  }

  void acceptError(folly::exception_wrapper ex) noexcept override {
    VLOG(5) << "acceptError";
    // unregister handlers while in the callback
    socket_->pauseAccepting();
    socket_->removeAcceptCallback(this, nullptr);
    error = std::move(ex);
    acceptFd = -1;
    baton_.post();
  }

  void acceptStarted() noexcept override { VLOG(5) << "acceptStarted"; }

  void acceptStopped() noexcept override { VLOG(5) << "acceptStopped"; }
};

} // namespace

namespace folly {
namespace coro {

ServerSocket::ServerSocket(
    std::shared_ptr<AsyncServerSocket> socket,
    std::optional<SocketAddress> bindAddr,
    uint32_t listenQueueDepth)
    : socket_{socket} {
  socket_->setReusePortEnabled(true);
  if (bindAddr.has_value()) {
    VLOG(1) << "ServerSocket binds on IP: " << bindAddr->describe();
    socket_->bind(*bindAddr);
  } else {
    VLOG(1) << "ServerSocket binds on any addr, random port";
    socket_->bind(0);
  }
  socket_->listen(listenQueueDepth);
}

Task<std::unique_ptr<Transport>> ServerSocket::accept() {
  VLOG(5) << "accept() called";
  co_await folly::coro::co_safe_point;

  Baton baton;
  AcceptCallback cb(baton, socket_);
  socket_->addAcceptCallback(&cb, nullptr);
  socket_->startAccepting();
  auto cancelToken = co_await folly::coro::co_current_cancellation_token;
  CancellationCallback cancellationCallback{
      cancelToken, [&baton] { baton.post(); }};

  co_await baton;
  if (cancelToken.isCancellationRequested()) {
    socket_->stopAccepting();
    co_yield co_cancelled;
  }
  if (cb.error) {
    co_yield co_error(std::move(cb.error));
  }
  co_return std::make_unique<Transport>(
      socket_->getEventBase(),
      AsyncSocket::newSocket(
          socket_->getEventBase(), NetworkSocket::fromFd(cb.acceptFd)));
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
