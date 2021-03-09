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

#pragma once

#include <folly/io/coro/Transport.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Shim class -- will remove
class Socket : public Transport {
 public:
  explicit Socket(AsyncSocket::UniquePtr socket)
      : Socket(socket->getEventBase(), std::move(socket)) {}

  Socket(
      folly::EventBase* eventBase, folly::AsyncTransport::UniquePtr transport)
      : Transport(eventBase, std::move(transport)) {}

  Socket(Transport&& transport) : Transport(std::move(transport)) {}

  static Task<Socket> connect(
      EventBase* evb,
      const SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout) {
    auto transport = co_await newConnectedSocket(evb, destAddr, connectTimeout);
    co_return Socket(std::move(transport));
  }
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
