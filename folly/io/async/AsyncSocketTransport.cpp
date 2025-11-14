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

#include <folly/io/async/AsyncSocketTransport.h>

namespace folly {

const SocketAddress& AsyncSocketTransport::anyAddress() {
  static const folly::Indestructible<SocketAddress> anyAddress =
      SocketAddress("0.0.0.0", 0);
  return anyAddress;
}

int AsyncSocketTransport::getNapiId() const {
#if defined(__linux__)
  int id = -1;
  socklen_t len = sizeof(id);
  auto ret = ::getsockopt(
      getNetworkSocket().toFd(), SOL_SOCKET, SO_INCOMING_NAPI_ID, &id, &len);
  return ret < 0 || id < 1 ? -1 : id;
#else
  return -1;
#endif
}

} // namespace folly
