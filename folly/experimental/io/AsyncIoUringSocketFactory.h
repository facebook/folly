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

#include <folly/experimental/io/AsyncIoUringSocket.h>
#include <folly/experimental/io/Liburing.h>

namespace folly {

class AsyncIoUringSocketFactory {
 public:
  static bool supports([[maybe_unused]] folly::EventBase* eb) {
#if FOLLY_HAS_LIBURING
    return AsyncIoUringSocket::supports(eb);
#else
    return false;
#endif
  }

  template <class TWrapper, class... Args>
  static TWrapper create([[maybe_unused]] Args&&... args) {
#if FOLLY_HAS_LIBURING
    return TWrapper(new AsyncIoUringSocket(std::forward<Args>(args)...));
#else
    throw std::runtime_error("AsyncIoUringSocket not supported");
#endif
  }

  static bool asyncDetachFd(
      [[maybe_unused]] AsyncTransport& transport,
      [[maybe_unused]] AsyncDetachFdCallback* callback) {
#if FOLLY_HAS_LIBURING
    AsyncIoUringSocket* socket =
        transport.getUnderlyingTransport<AsyncIoUringSocket>();
    if (socket) {
      socket->asyncDetachFd(callback);
      return true;
    }
#endif

    return false;
  }
};

} // namespace folly
