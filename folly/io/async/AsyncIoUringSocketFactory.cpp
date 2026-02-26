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

#include <folly/Conv.h>
#include <folly/Random.h>
#include <folly/io/async/IoUringBackend.h>
#include <folly/portability/Sockets.h>

namespace folly {

NetworkSocket AsyncIoUringSocketFactory::createBoundSocketForZcRx(
    [[maybe_unused]] folly::EventBase* evb,
    [[maybe_unused]] const folly::IPAddress& destAddr,
    [[maybe_unused]] uint16_t destPort) {
#if FOLLY_HAS_LIBURING
  auto* backend = dynamic_cast<folly::IoUringBackend*>(evb->getBackend());
  if (backend == nullptr) {
    throw std::runtime_error("createBoundSocketForZcRx: not an IoUringBackend");
  }

  auto family = destAddr.isV4() ? AF_INET : AF_INET6;
  int sockfd = ::socket(family, SOCK_STREAM, 0);
  if (sockfd < 0) {
    throw std::runtime_error(
        "createBoundSocketForZcRx: socket() failed, errno=" +
        folly::to<std::string>(errno));
  }

  // Use non-privileged ports below ip_local_port_range (32768) to avoid
  // collisions with kernel auto-assigned ephemeral ports.
  constexpr uint16_t kMinPort = 1024;
  constexpr uint16_t kMaxPort = 32768;
  constexpr uint16_t kPortRange = kMaxPort - kMinPort;
  uint16_t startPort = kMinPort + (folly::Random::rand32() % kPortRange);

  // Find a port that hashes to the target ZC-RX queue and bind to it.
  // computeSrcPortForQueueId searches the range with wrap-around.
  // On EADDRINUSE, advance past the failed port and search again.
  constexpr int kMaxRetries = 1000;
  for (int retry = 0; retry < kMaxRetries; retry++) {
    int port = backend->computeSrcPortForQueueId(
        destAddr, destPort, startPort, kMinPort, kMaxPort);
    if (port == -1) {
      break;
    }

    struct sockaddr_storage bindStorage = {};
    socklen_t bindLen;
    if (family == AF_INET6) {
      auto* sa = reinterpret_cast<struct sockaddr_in6*>(&bindStorage);
      sa->sin6_family = AF_INET6;
      sa->sin6_port = htons(static_cast<uint16_t>(port));
      sa->sin6_addr = in6addr_any;
      bindLen = sizeof(struct sockaddr_in6);
    } else {
      auto* sa = reinterpret_cast<struct sockaddr_in*>(&bindStorage);
      sa->sin_family = AF_INET;
      sa->sin_port = htons(static_cast<uint16_t>(port));
      sa->sin_addr.s_addr = INADDR_ANY;
      bindLen = sizeof(struct sockaddr_in);
    }

    if (::bind(
            sockfd, reinterpret_cast<const sockaddr*>(&bindStorage), bindLen) ==
        0) {
      VLOG(2) << "createBoundSocketForZcRx: bound to srcPort=" << port
              << " for destAddr=" << destAddr << ", destPort=" << destPort;
      return NetworkSocket(sockfd);
    }

    // EADDRINUSE is expected, advance to find next valid port
    startPort = static_cast<uint16_t>(port) + 1;
    if (startPort >= kMaxPort) {
      startPort = kMinPort;
    }
  }

  ::close(sockfd);
  throw std::runtime_error(
      "createBoundSocketForZcRx: no bindable port for destAddr=" +
      folly::to<std::string>(destAddr) +
      ", destPort=" + folly::to<std::string>(destPort));
#else
  throw std::runtime_error("createBoundSocketForZcRx: io_uring not supported");
#endif
}

} // namespace folly
