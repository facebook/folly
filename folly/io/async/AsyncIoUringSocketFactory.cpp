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

#include <folly/io/async/AsyncIoUringSocketFactory.h>

#include <folly/io/async/IoUringBackend.h>

namespace folly {

bool AsyncIoUringSocketFactory::bindSocketForZcRx(
    AsyncSocketTransport& transport,
    const folly::IPAddress& destAddr,
    uint16_t destPort) {
#if FOLLY_HAS_LIBURING
  auto* backend = dynamic_cast<folly::IoUringBackend*>(
      transport.getEventBase()->getBackend());
  if (backend == nullptr) {
    LOG(ERROR) << "bindSocketForZcRx: failed to get IoUringBackend";
    return false;
  }

  int srcPort = backend->computeSrcPortForQueueId(destAddr, destPort);
#else
  int srcPort = -1;
#endif

  if (srcPort == -1) {
    LOG(ERROR) << "bindSocketForZcRx: failed to resolve src port for destAddr="
               << destAddr << ", destPort=" << destPort;
    return false;
  }

  SocketAddress bindAddr(transport.getLocalAddress().getAddressStr(), srcPort);
  sockaddr_storage storage{};
  bindAddr.getAddress(&storage);

  int fd = transport.getNetworkSocket().toFd();
  if (::bind(
          fd,
          reinterpret_cast<const sockaddr*>(&storage),
          bindAddr.getActualSize()) < 0) {
    LOG(ERROR) << "bindSocketForZcRx: failed to bind socket, errno=" << errno
               << ", srcPort=" << srcPort;
    return false;
  }
  return true;
}

} // namespace folly
