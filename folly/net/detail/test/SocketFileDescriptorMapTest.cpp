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

#include <folly/net/detail/SocketFileDescriptorMap.h>

#include <folly/Portability.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Windows.h>

namespace folly {
namespace netops {

using folly::netops::detail::SocketFileDescriptorMap;
namespace fsp = folly::portability::sockets;

TEST(SocketFileDescriptorMap, fdToSocketConsistent) {
  auto fd = fsp::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  auto sockA = SocketFileDescriptorMap::fdToSocket(fd);
  auto sockB = SocketFileDescriptorMap::fdToSocket(fd);
  EXPECT_EQ(sockA, sockB);

  int fd2 = SocketFileDescriptorMap::socketToFd(sockA);
  EXPECT_EQ(fd, fd2);

  SocketFileDescriptorMap::close(fd);
}

TEST(SocketFileDescriptorMap, noSocketReuse) {
  auto sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int fdA = SocketFileDescriptorMap::socketToFd(sock);
  int fdB = SocketFileDescriptorMap::socketToFd(sock);
  EXPECT_EQ(fdA, fdB);

  auto sock2 = SocketFileDescriptorMap::fdToSocket(fdA);
  EXPECT_EQ(sock, sock2);

  SocketFileDescriptorMap::close(sock);

  // We're on Windows, so let's do some additional testing to ensure
  // that we aren't using stale entries in the map.
  // This is guarded to only on Windows because we need to assert on
  // the specifics of how file descriptors & sockets are allocated,
  // which varies between platforms.
  if (kIsWindows) {
    auto sock3 = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // Nothing else is running in this context, so we should,
    // in theory, have re-allocated the same socket/file descriptor.
    EXPECT_EQ(sock, sock3);

    // Now we mess up the order by creating a new FD.
    int devNull = ::open("/dev/null", O_RDONLY);
    EXPECT_EQ(devNull, fdA);

    int fdC = SocketFileDescriptorMap::socketToFd(sock);
    EXPECT_NE(fdA, fdC);

    SocketFileDescriptorMap::close(sock3);
    ::close(devNull);
  }
}

TEST(SocketFileDescriptorMap, closeNonMappedNativeSocket) {
  auto sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  SocketFileDescriptorMap::close(sock);
}

} // namespace netops
} // namespace folly
