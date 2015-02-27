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
#include <iostream>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBase.h>

#include <gtest/gtest.h>

namespace folly {

TEST(AsyncSocketTest, getSockOpt) {
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, 0);

  int val;
  socklen_t len;

  int expectedRc = getsockopt(socket->getFd(), SOL_SOCKET,
                              SO_REUSEADDR, &val, &len);
  int actualRc = socket->getSockOpt(SOL_SOCKET, SO_REUSEADDR, &val, &len);

  EXPECT_EQ(expectedRc, actualRc);
}

TEST(AsyncSocketTest, REUSEPORT) {
  EventBase base;
  auto serverSocket = AsyncServerSocket::newSocket(&base);
  serverSocket->bind(0);
  serverSocket->listen(0);
  serverSocket->startAccepting();

  try {
    serverSocket->setReusePortEnabled(true);
  } catch(...) {
    LOG(INFO) << "Reuse port probably not supported";
    return;
  }

  SocketAddress address;
  serverSocket->getAddress(&address);
  int port = address.getPort();

  auto serverSocket2 = AsyncServerSocket::newSocket(&base);
  serverSocket2->setReusePortEnabled(true);
  serverSocket2->bind(port);
  serverSocket2->listen(0);
  serverSocket2->startAccepting();

}

TEST(AsyncSocketTest, v4v6samePort) {
  EventBase base;
  auto serverSocket = AsyncServerSocket::newSocket(&base);
  serverSocket->bind(0);
  auto addrs = serverSocket->getAddresses();
  ASSERT_GT(addrs.size(), 0);
  uint16_t port = addrs[0].getPort();
  for (const auto& addr : addrs) {
    EXPECT_EQ(port, addr.getPort());
  }
}

} // namespace
