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

#include <folly/io/async/AsyncSocket.h>

#include <iostream>

#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace testing;

#ifndef TCP_SAVE_SYN
#define TCP_SAVE_SYN 27
#endif

TEST(AsyncSocketTest, getSockOpt) {
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
      AsyncSocket::newSocket(&evb, NetworkSocket(0));

  int val;
  socklen_t len;

  int expectedRc = netops::getsockopt(
      socket->getNetworkSocket(), SOL_SOCKET, SO_REUSEADDR, &val, &len);
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
  } catch (...) {
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

TEST(AsyncSocketTest, DisableReuseAddr) {
  EventBase base;
  auto serverSocket = AsyncServerSocket::newSocket(&base);
  serverSocket->setEnableReuseAddr(false /* enable */);
  // idempotent
  serverSocket->setEnableReuseAddr(false /* enable */);
  serverSocket->setEnableReuseAddr(false /* enable */);
  serverSocket->bind(0);

  SocketAddress address;
  serverSocket->getAddress(&address);
  int port = address.getPort();

  auto serverSocket2 = AsyncServerSocket::newSocket(&base);
  serverSocket2->setEnableReuseAddr(false /* enable */);
  // idempotent
  serverSocket2->setEnableReuseAddr(false /* enable */);
  serverSocket2->setEnableReuseAddr(false /* enable */);
  EXPECT_THROW(serverSocket2->bind(port), std::system_error);
  // it's ok to bind to a different port
  serverSocket2->bind(0);
}

TEST(AsyncSocketTest, EnableThenDisableReuseAddr) {
  EventBase base;
  auto serverSocket = AsyncServerSocket::newSocket(&base);
  serverSocket->bind(0);

  SocketAddress address;
  serverSocket->getAddress(&address);
  int port = address.getPort();

  auto serverSocket2 = AsyncServerSocket::newSocket(&base);
  // defaulty SO_REUSEADDR enabled so can bind to same port
  serverSocket2->bind(port);
  serverSocket2->setEnableReuseAddr(false /* enable */);
  serverSocket->setEnableReuseAddr(false /* enable */);

  EXPECT_THROW(serverSocket2->bind(port), std::system_error);
  // it's ok to bind to a different port
  serverSocket2->bind(0);
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

TEST(AsyncSocketTest, duplicateBind) {
  EventBase base;
  auto server1 = AsyncServerSocket::newSocket(&base);
  server1->bind(0);
  server1->listen(10);

  SocketAddress address;
  server1->getAddress(std::addressof(address));

  auto server2 = AsyncServerSocket::newSocket(&base);
  EXPECT_THROW(server2->bind(address.getPort()), std::exception);
}

TEST(AsyncSocketTest, tosReflect) {
  EventBase base;
  auto server1 = AsyncServerSocket::newSocket(&base);
  server1->bind(0);
  server1->listen(10);
  auto fd = server1->getNetworkSocket();

  // Verify if tos reflect is disabled by default
  // and the TCP_SAVE_SYN setting is not enabled
  EXPECT_FALSE(server1->getTosReflect());
  int value;
  socklen_t valueLength = sizeof(value);
  int rc =
      netops::getsockopt(fd, IPPROTO_TCP, TCP_SAVE_SYN, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 0);

  // Enable TOS reflect on the server socket
  server1->setTosReflect(true);

  // Verify if tos reflect is enabled now
  // and the TCP_SAVE_SYN setting is also enabled
  EXPECT_TRUE(server1->getTosReflect());
  rc = netops::getsockopt(fd, IPPROTO_TCP, TCP_SAVE_SYN, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 1);
}

TEST(AsyncSocketTest, listenerTosV6) {
  EventBase base;
  auto server1 = AsyncServerSocket::newSocket(&base);
  server1->bind(0);
  server1->listen(10);
  auto fd = server1->getNetworkSocket();

  // Verify if Listener TOS is disabled by default
  EXPECT_FALSE(server1->getListenerTos());
  int value;
  socklen_t valueLength = sizeof(value);
  int rc =
      netops::getsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 0);

  // Set listener Tos to 116 (0x74, represents dscp 29)
  server1->setListenerTos(116);

  // Verify if listener DSCP is set now
  EXPECT_TRUE(server1->getListenerTos());
  rc = netops::getsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 116);
}

TEST(AsyncSocketTest, listenerTosV4) {
  EventBase base;
  auto server1 = AsyncServerSocket::newSocket(&base);
  folly::IPAddress ip("127.0.0.1");
  std::vector<folly::IPAddress> serverIp;
  serverIp.push_back(ip);
  server1->bind(serverIp, 0);
  server1->listen(10);
  auto fd = server1->getNetworkSocket();

  // Verify if Listener TOS is disabled by default
  EXPECT_FALSE(server1->getListenerTos());
  int value;
  socklen_t valueLength = sizeof(value);
  int rc = netops::getsockopt(fd, IPPROTO_IP, IP_TOS, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 0);

  // Set listener Tos to 140 (0x8c, represents dscp 35)
  server1->setListenerTos(140);

  // Verify if listener DSCP is set now
  EXPECT_TRUE(server1->getListenerTos());
  rc = netops::getsockopt(fd, IPPROTO_IP, IP_TOS, &value, &valueLength);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(value, 140);
}
