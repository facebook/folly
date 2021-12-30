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

#pragma once

#include <optional>

#include <folly/ExceptionWrapper.h>
#include <folly/Expected.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/coro/Transport.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

//
// This server socket will accept connections on the
// same event base as the socket itself
//
class ServerSocket {
 public:
  ServerSocket(
      std::shared_ptr<AsyncServerSocket> socket,
      std::optional<SocketAddress> bindAddr,
      uint32_t listenQueueDepth);

  ServerSocket(ServerSocket&&) = default;
  ServerSocket& operator=(ServerSocket&&) = default;

  Task<std::unique_ptr<Transport>> accept();

  void close() noexcept {
    if (socket_) {
      socket_->stopAccepting();
    }
  }

  const AsyncServerSocket* getAsyncServerSocket() const {
    return socket_.get();
  }

 private:
  // non-copyable
  ServerSocket(const ServerSocket&) = delete;
  ServerSocket& operator=(const ServerSocket&) = delete;

  std::shared_ptr<AsyncServerSocket> socket_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
