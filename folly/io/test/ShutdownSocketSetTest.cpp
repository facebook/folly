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

#include <folly/io/ShutdownSocketSet.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <glog/logging.h>

#include <folly/net/NetOps.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

namespace folly {
namespace test {

class Server {
 public:
  Server();

  void stop(bool abortive);
  void join();
  int port() const {
    return port_;
  }
  int closeClients(bool abortive);

  void shutdownAll(bool abortive);

 private:
  NetworkSocket acceptSocket_;
  int port_;
  enum StopMode { NO_STOP, ORDERLY, ABORTIVE };
  std::atomic<StopMode> stop_;
  std::thread serverThread_;
  std::vector<NetworkSocket> fds_;
  folly::ShutdownSocketSet shutdownSocketSet_;
  folly::Baton<> baton_;
};

Server::Server() : acceptSocket_(), port_(0), stop_(NO_STOP) {
  acceptSocket_ = netops::socket(PF_INET, SOCK_STREAM, 0);
  CHECK_NE(acceptSocket_, NetworkSocket());
  shutdownSocketSet_.add(acceptSocket_);

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  CHECK_ERR(netops::bind(
      acceptSocket_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)));

  CHECK_ERR(netops::listen(acceptSocket_, 10));

  socklen_t addrLen = sizeof(addr);
  CHECK_ERR(netops::getsockname(
      acceptSocket_, reinterpret_cast<sockaddr*>(&addr), &addrLen));

  port_ = ntohs(addr.sin_port);

  serverThread_ = std::thread([this] {
    bool first = true;
    while (stop_ == NO_STOP) {
      sockaddr_in peer;
      socklen_t peerLen = sizeof(peer);
      auto fd = netops::accept(
          acceptSocket_, reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (fd == NetworkSocket()) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EINVAL || errno == ENOTSOCK) { // socket broken
          break;
        }
      }
      CHECK_NE(fd, NetworkSocket());
      shutdownSocketSet_.add(fd);
      fds_.push_back(fd);
      CHECK(first);
      first = false;
      baton_.post();
    }

    if (stop_ != NO_STOP) {
      closeClients(stop_ == ABORTIVE);
    }

    shutdownSocketSet_.close(acceptSocket_);
    acceptSocket_ = NetworkSocket();
    port_ = 0;
  });
}

int Server::closeClients(bool abortive) {
  for (auto fd : fds_) {
    if (abortive) {
      struct linger l = {1, 0};
      CHECK_ERR(netops::setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)));
    }
    shutdownSocketSet_.close(fd);
  }
  int n = fds_.size();
  fds_.clear();
  return n;
}

void Server::shutdownAll(bool abortive) {
  baton_.wait();
  shutdownSocketSet_.shutdownAll(abortive);
}

void Server::stop(bool abortive) {
  stop_ = abortive ? ABORTIVE : ORDERLY;
  netops::shutdown(acceptSocket_, SHUT_RDWR);
}

void Server::join() {
  serverThread_.join();
}

NetworkSocket createConnectedSocket(int port) {
  auto sock = netops::socket(PF_INET, SOCK_STREAM, 0);
  CHECK_NE(sock, NetworkSocket());
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl((127 << 24) | 1); // XXX
  CHECK_ERR(netops::connect(
      sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)));
  return sock;
}

void runCloseTest(bool abortive) {
  Server server;

  auto sock = createConnectedSocket(server.port());

  std::thread stopper([&server, abortive] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.stop(abortive);
    server.join();
  });

  char c;
  int r = netops::recv(sock, &c, 1, 0);
  if (abortive) {
    int e = errno;
    EXPECT_EQ(-1, r);
    EXPECT_EQ(ECONNRESET, e);
  } else {
    EXPECT_EQ(0, r);
  }

  netops::close(sock);

  stopper.join();

  EXPECT_EQ(0, server.closeClients(false)); // closed by server when it exited
}

TEST(ShutdownSocketSetTest, OrderlyClose) {
  runCloseTest(false);
}

TEST(ShutdownSocketSetTest, AbortiveClose) {
  runCloseTest(true);
}

void runKillTest(bool abortive) {
  Server server;

  auto sock = createConnectedSocket(server.port());

  std::thread killer([&server, abortive] {
    server.shutdownAll(abortive);
    server.join();
  });

  char c;
  int r = netops::recv(sock, &c, 1, 0);

  // "abortive" is just a hint for ShutdownSocketSet, so accept both
  // behaviors
  if (abortive) {
    if (r == -1) {
      EXPECT_EQ(ECONNRESET, errno);
    } else {
      EXPECT_EQ(r, 0);
    }
  } else {
    EXPECT_EQ(0, r);
  }

  netops::close(sock);

  killer.join();

  // NOT closed by server when it exited
  EXPECT_EQ(1, server.closeClients(false));
}

TEST(ShutdownSocketSetTest, OrderlyKill) {
  runKillTest(false);
}

TEST(ShutdownSocketSetTest, AbortiveKill) {
  runKillTest(true);
}
} // namespace test
} // namespace folly
