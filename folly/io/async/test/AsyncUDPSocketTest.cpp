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

#include <thread>

#include <folly/Conv.h>
#include <folly/SocketAddress.h>
#include <folly/String.h>
#include <folly/experimental/TestUtil.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>

using folly::AsyncTimeout;
using folly::AsyncUDPServerSocket;
using folly::AsyncUDPSocket;
using folly::errnoStr;
using folly::EventBase;
using folly::SocketAddress;
using namespace testing;

using OnDataAvailableParams =
    folly::AsyncUDPSocket::ReadCallback::OnDataAvailableParams;

class UDPAcceptor : public AsyncUDPServerSocket::Callback {
 public:
  UDPAcceptor(EventBase* evb, int n, bool changePortForWrites)
      : evb_(evb), n_(n), changePortForWrites_(changePortForWrites) {}

  void onListenStarted() noexcept override {}

  void onListenStopped() noexcept override {}

  void onDataAvailable(
      std::shared_ptr<folly::AsyncUDPSocket> socket,
      const folly::SocketAddress& client,
      std::unique_ptr<folly::IOBuf> data,
      bool truncated,
      OnDataAvailableParams) noexcept override {
    lastClient_ = client;
    lastMsg_ = data->clone()->moveToFbString().toStdString();

    auto len = data->computeChainDataLength();
    VLOG(4) << "Worker " << n_ << " read " << len << " bytes "
            << "(trun:" << truncated << ") from " << client.describe() << " - "
            << lastMsg_;

    sendPong(socket);
  }

  void sendPong(std::shared_ptr<folly::AsyncUDPSocket> socket) noexcept {
    try {
      auto writeSocket = socket;
      if (changePortForWrites_) {
        writeSocket = std::make_shared<folly::AsyncUDPSocket>(evb_);
        writeSocket->setReuseAddr(false);
        writeSocket->bind(folly::SocketAddress("127.0.0.1", 0));
      }
      writeSocket->write(lastClient_, folly::IOBuf::copyBuffer(lastMsg_));
    } catch (const std::exception& ex) {
      VLOG(4) << "Failed to send PONG " << ex.what();
    }
  }

 private:
  EventBase* const evb_{nullptr};
  const int n_{-1};
  // Whether to create a new port per write.
  bool changePortForWrites_{true};

  folly::SocketAddress lastClient_;
  std::string lastMsg_;
};

class UDPServer {
 public:
  UDPServer(EventBase* evb, folly::SocketAddress addr, int n)
      : evb_(evb), addr_(addr), evbs_(n) {}

  void start() {
    CHECK(evb_->isInEventBaseThread());

    socket_ = std::make_unique<AsyncUDPServerSocket>(evb_, 1500);

    try {
      socket_->bind(addr_);
      VLOG(4) << "Server listening on " << socket_->address().describe();
    } catch (const std::exception& ex) {
      LOG(FATAL) << ex.what();
    }

    acceptors_.reserve(evbs_.size());
    threads_.reserve(evbs_.size());

    // Add numWorkers thread
    int i = 0;
    for (auto& evb : evbs_) {
      acceptors_.emplace_back(&evb, i, changePortForWrites_);

      std::thread t([&]() { evb.loopForever(); });

      evb.waitUntilRunning();

      socket_->addListener(&evb, &acceptors_[i]);
      threads_.emplace_back(std::move(t));
      ++i;
    }

    socket_->listen();
  }

  folly::SocketAddress address() const {
    return socket_->address();
  }

  void shutdown() {
    CHECK(evb_->isInEventBaseThread());
    socket_->close();
    socket_.reset();

    for (auto& evb : evbs_) {
      evb.terminateLoopSoon();
    }

    for (auto& t : threads_) {
      t.join();
    }
  }

  void pauseAccepting() {
    socket_->pauseAccepting();
  }

  void resumeAccepting() {
    socket_->resumeAccepting();
  }

  bool isAccepting() {
    return socket_->isAccepting();
  }

  // Whether writes from the UDP server should change the port for each message.
  void setChangePortForWrites(bool changePortForWrites) {
    changePortForWrites_ = changePortForWrites;
  }

 private:
  EventBase* const evb_{nullptr};
  const folly::SocketAddress addr_;

  std::unique_ptr<AsyncUDPServerSocket> socket_;
  std::vector<std::thread> threads_;
  std::vector<folly::EventBase> evbs_;
  std::vector<UDPAcceptor> acceptors_;
  bool changePortForWrites_{true};
};

class UDPClient : private AsyncUDPSocket::ReadCallback, private AsyncTimeout {
 public:
  using AsyncUDPSocket::ReadCallback::OnDataAvailableParams;

  ~UDPClient() override = default;

  explicit UDPClient(EventBase* evb) : AsyncTimeout(evb), evb_(evb) {}

  void
  start(const folly::SocketAddress& server, int n, bool sendClustered = false) {
    CHECK(evb_->isInEventBaseThread());
    server_ = server;
    socket_ = std::make_unique<AsyncUDPSocket>(evb_);

    try {
      socket_->bind(folly::SocketAddress("127.0.0.1", 0));
      if (connectAddr_) {
        connect();
      }
      VLOG(2) << "Client bound to " << socket_->address().describe();
    } catch (const std::exception& ex) {
      LOG(FATAL) << ex.what();
    }

    socket_->resumeRead(this);

    n_ = n;

    // Start playing ping pong
    if (sendClustered) {
      sendPingsClustered();
    } else {
      sendPing();
    }
  }

  void connect() {
    int ret = socket_->connect(*connectAddr_);
    if (ret != 0) {
      throw folly::AsyncSocketException(
          folly::AsyncSocketException::NOT_OPEN, "ConnectFail", errno);
    }
    VLOG(2) << "Client connected to address=" << *connectAddr_;
  }

  void shutdown() {
    CHECK(evb_->isInEventBaseThread());
    socket_->pauseRead();
    socket_->close();
    socket_.reset();
    evb_->terminateLoopSoon();
  }

  void sendPing() {
    if (n_ == 0) {
      shutdown();
      return;
    }

    --n_;
    scheduleTimeout(5);
    writePing(folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
  }

  void sendPingsClustered() {
    scheduleTimeout(5);
    while (n_ > 0) {
      --n_;
      writePing(folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
    }
  }

  virtual void writePing(std::unique_ptr<folly::IOBuf> buf) {
    auto ret = socket_->write(server_, std::move(buf));
    if (ret == -1) {
      error_ = true;
    }
  }

  void getReadBuffer(void** buf, size_t* len) noexcept override {
    *buf = buf_;
    *len = 1024;
  }

  void onDataAvailable(
      const folly::SocketAddress& client,
      size_t len,
      bool truncated,
      OnDataAvailableParams) noexcept override {
    VLOG(4) << "Read " << len << " bytes (trun:" << truncated << ") from "
            << client.describe() << " - " << std::string(buf_, len);
    VLOG(4) << n_ << " left";

    ++pongRecvd_;

    sendPing();
  }

  void onReadError(const folly::AsyncSocketException& ex) noexcept override {
    VLOG(4) << ex.what();

    // Start listening for next PONG
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept override {
    CHECK(false) << "We unregister reads before closing";
  }

  void timeoutExpired() noexcept override {
    VLOG(4) << "Timeout expired";
    sendPing();
  }

  int pongRecvd() const {
    return pongRecvd_;
  }

  AsyncUDPSocket& getSocket() {
    return *socket_;
  }

  void setShouldConnect(const folly::SocketAddress& connectAddr) {
    connectAddr_ = connectAddr;
  }

  bool error() const {
    return error_;
  }

  void incrementPongCount(int n) {
    pongRecvd_ += n;
  }

 protected:
  folly::Optional<folly::SocketAddress> connectAddr_;
  EventBase* const evb_{nullptr};

  folly::SocketAddress server_;
  std::unique_ptr<AsyncUDPSocket> socket_;
  bool error_{false};

 private:
  int pongRecvd_{0};

  int n_{0};
  char buf_[1024];
};

class UDPNotifyClient : public UDPClient {
 public:
  ~UDPNotifyClient() override = default;

  explicit UDPNotifyClient(
      EventBase* evb,
      bool useRecvmmsg = false,
      unsigned int numMsgs = 1)
      : UDPClient(evb), useRecvmmsg_(useRecvmmsg), numMsgs_(numMsgs) {}

  bool shouldOnlyNotify() override {
    return true;
  }

  void onRecvMsg(AsyncUDPSocket& sock) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    void* buf{};
    size_t len{};
    getReadBuffer(&buf, &len);

    iovec vec;
    vec.iov_base = buf;
    vec.iov_len = len;
    struct sockaddr_storage addrStorage;
    socklen_t addrLen = sizeof(addrStorage);
    memset(&addrStorage, 0, size_t(addrLen));
    auto rawAddr = reinterpret_cast<sockaddr*>(&addrStorage);
    rawAddr->sa_family = socket_->address().getFamily();

    msg.msg_name = rawAddr;
    msg.msg_namelen = addrLen;
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;

    ssize_t ret = sock.recvmsg(&msg, 0);
    if (ret < 0) {
      if (errno != EAGAIN || errno != EWOULDBLOCK) {
        onReadError(folly::AsyncSocketException(
            folly::AsyncSocketException::NETWORK_ERROR, "error"));
        return;
      }
    }
    SocketAddress addr;
    addr.setFromSockaddr(rawAddr, addrLen);

    onDataAvailable(addr, size_t(read), false, OnDataAvailableParams());
  }

  void onRecvMmsg(AsyncUDPSocket& sock) {
    std::vector<struct mmsghdr> msgs;
    msgs.reserve(numMsgs_);
    memset(msgs.data(), 0, sizeof(struct mmsghdr) * numMsgs_);

    const socklen_t addrLen = sizeof(struct sockaddr_storage);

    const size_t dataSize = 1024;
    std::vector<char> buf;
    buf.reserve(numMsgs_ * dataSize);
    memset(buf.data(), 0, numMsgs_ * dataSize);

    std::vector<struct sockaddr_storage> addrs;
    addrs.reserve(numMsgs_);
    memset(addrs.data(), 0, sizeof(struct sockaddr_storage) * numMsgs_);

    std::vector<struct iovec> iovecs;
    iovecs.reserve(numMsgs_);
    memset(iovecs.data(), 0, sizeof(struct iovec) * numMsgs_);

    for (unsigned int i = 0; i < numMsgs_; ++i) {
      struct msghdr* msg = &msgs[i].msg_hdr;

      auto rawAddr = reinterpret_cast<sockaddr*>(&addrs[i]);
      rawAddr->sa_family = socket_->address().getFamily();

      iovecs[i].iov_base = &buf[i * dataSize];
      iovecs[i].iov_len = dataSize;

      msg->msg_name = rawAddr;
      msg->msg_namelen = addrLen;
      msg->msg_iov = &iovecs[i];
      msg->msg_iovlen = 1;
    }

    int ret = sock.recvmmsg(
        msgs.data(), numMsgs_, 0x10000 /* MSG_WAITFORONE */, nullptr);
    if (ret < 0) {
      if (errno != EAGAIN || errno != EWOULDBLOCK) {
        onReadError(folly::AsyncSocketException(
            folly::AsyncSocketException::NETWORK_ERROR, "error"));
      }
      return;
    }

    incrementPongCount(ret);

    if (pongRecvd() == (int)numMsgs_) {
      shutdown();
    } else {
      onRecvMmsg(sock);
    }
  }

  void onNotifyDataAvailable(AsyncUDPSocket& sock) noexcept override {
    notifyInvoked = true;
    if (useRecvmmsg_) {
      onRecvMmsg(sock);
    } else {
      onRecvMsg(sock);
    }
  }

  bool notifyInvoked{false};
  bool useRecvmmsg_{false};
  unsigned int numMsgs_{1};
};

class AsyncSocketIntegrationTest : public Test {
 public:
  void SetUp() override {
    server = std::make_unique<UDPServer>(
        &sevb, folly::SocketAddress("127.0.0.1", 0), 4);

    // Start event loop in a separate thread
    serverThread =
        std::make_unique<std::thread>([this]() { sevb.loopForever(); });

    // Wait for event loop to start
    sevb.waitUntilRunning();
  }

  void startServer() {
    // Start the server
    sevb.runInEventBaseThreadAndWait([&]() { server->start(); });
    LOG(INFO) << "Server listening=" << server->address();
  }

  void TearDown() override {
    // Shutdown server
    sevb.runInEventBaseThread([&]() {
      server->shutdown();
      sevb.terminateLoopSoon();
    });

    // Wait for server thread to join
    serverThread->join();
  }

  std::unique_ptr<UDPClient> performPingPongTest(
      folly::SocketAddress writeAddress,
      folly::Optional<folly::SocketAddress> connectedAddress);

  std::unique_ptr<UDPNotifyClient> performPingPongNotifyTest(
      folly::SocketAddress writeAddress,
      folly::Optional<folly::SocketAddress> connectedAddress);

  std::unique_ptr<UDPNotifyClient> performPingPongNotifyMmsgTest(
      folly::SocketAddress writeAddress,
      unsigned int numMsgs,
      folly::Optional<folly::SocketAddress> connectedAddress);

  folly::EventBase sevb;
  folly::EventBase cevb;
  std::unique_ptr<std::thread> serverThread;
  std::unique_ptr<UDPServer> server;
};

std::unique_ptr<UDPClient> AsyncSocketIntegrationTest::performPingPongTest(
    folly::SocketAddress writeAddress,
    folly::Optional<folly::SocketAddress> connectedAddress) {
  auto client = std::make_unique<UDPClient>(&cevb);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress);
  }
  // Start event loop in a separate thread
  auto clientThread = std::thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(writeAddress, 100); });

  // Wait for client to finish
  clientThread.join();
  return client;
}

std::unique_ptr<UDPNotifyClient>
AsyncSocketIntegrationTest::performPingPongNotifyTest(
    folly::SocketAddress writeAddress,
    folly::Optional<folly::SocketAddress> connectedAddress) {
  auto client = std::make_unique<UDPNotifyClient>(&cevb);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress);
  }
  // Start event loop in a separate thread
  auto clientThread = std::thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(writeAddress, 100); });

  // Wait for client to finish
  clientThread.join();
  return client;
}

std::unique_ptr<UDPNotifyClient>
AsyncSocketIntegrationTest::performPingPongNotifyMmsgTest(
    folly::SocketAddress writeAddress,
    unsigned int numMsgs,
    folly::Optional<folly::SocketAddress> connectedAddress) {
  auto client = std::make_unique<UDPNotifyClient>(&cevb, true, numMsgs);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress);
  }
  // Start event loop in a separate thread
  auto clientThread = std::thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread(
      [&]() { client->start(writeAddress, numMsgs, true); });

  // Wait for client to finish
  clientThread.join();
  return client;
}

TEST_F(AsyncSocketIntegrationTest, PingPong) {
  startServer();
  auto pingClient = performPingPongTest(server->address(), folly::none);
  // This should succeed.
  ASSERT_GT(pingClient->pongRecvd(), 0);
}

TEST_F(AsyncSocketIntegrationTest, PingPongNotify) {
  startServer();
  auto pingClient = performPingPongNotifyTest(server->address(), folly::none);
  // This should succeed.
  ASSERT_GT(pingClient->pongRecvd(), 0);
  ASSERT_TRUE(pingClient->notifyInvoked);
}

TEST_F(AsyncSocketIntegrationTest, PingPongNotifyMmsg) {
  startServer();
  auto pingClient =
      performPingPongNotifyMmsgTest(server->address(), 10, folly::none);
  // This should succeed.
  ASSERT_EQ(pingClient->pongRecvd(), 10);
  ASSERT_TRUE(pingClient->notifyInvoked);
}

TEST_F(AsyncSocketIntegrationTest, ConnectedPingPong) {
  server->setChangePortForWrites(false);
  startServer();
  auto pingClient = performPingPongTest(server->address(), server->address());
  // This should succeed
  ASSERT_GT(pingClient->pongRecvd(), 0);
}

TEST_F(AsyncSocketIntegrationTest, ConnectedPingPongServerWrongAddress) {
  server->setChangePortForWrites(true);
  startServer();
  auto pingClient = performPingPongTest(server->address(), server->address());
  // This should fail.
  ASSERT_EQ(pingClient->pongRecvd(), 0);
}

TEST_F(AsyncSocketIntegrationTest, ConnectedPingPongClientWrongAddress) {
  server->setChangePortForWrites(false);
  startServer();
  folly::SocketAddress connectAddr(
      server->address().getIPAddress(), server->address().getPort() + 1);
  auto pingClient = performPingPongTest(server->address(), connectAddr);
  // This should fail.
  ASSERT_EQ(pingClient->pongRecvd(), 0);
  EXPECT_TRUE(pingClient->error());
}

TEST_F(AsyncSocketIntegrationTest, ConnectedPingPongDifferentWriteAddress) {
  server->setChangePortForWrites(false);
  startServer();
  folly::SocketAddress connectAddr(
      server->address().getIPAddress(), server->address().getPort() + 1);
  auto pingClient = performPingPongTest(connectAddr, server->address());
  // This should fail.
  ASSERT_EQ(pingClient->pongRecvd(), 0);
  EXPECT_TRUE(pingClient->error());
}

TEST_F(AsyncSocketIntegrationTest, PingPongPauseResumeListening) {
  startServer();

  // Exchange should not happen when paused.
  server->pauseAccepting();
  EXPECT_FALSE(server->isAccepting());
  auto pausedClient = performPingPongTest(server->address(), folly::none);
  ASSERT_EQ(pausedClient->pongRecvd(), 0);

  // Exchange does occur after resuming.
  server->resumeAccepting();
  EXPECT_TRUE(server->isAccepting());
  auto pingClient = performPingPongTest(server->address(), folly::none);
  ASSERT_GT(pingClient->pongRecvd(), 0);
}

class MockErrMessageCallback : public AsyncUDPSocket::ErrMessageCallback {
 public:
  ~MockErrMessageCallback() override = default;

  MOCK_METHOD1(errMessage_, void(const cmsghdr&));
  void errMessage(const cmsghdr& cmsg) noexcept override {
    errMessage_(cmsg);
  }

  MOCK_METHOD1(errMessageError_, void(const folly::AsyncSocketException&));
  void errMessageError(
      const folly::AsyncSocketException& ex) noexcept override {
    errMessageError_(ex);
  }
};

class MockUDPReadCallback : public AsyncUDPSocket::ReadCallback {
 public:
  ~MockUDPReadCallback() override = default;

  MOCK_METHOD2(getReadBuffer_, void(void**, size_t*));
  void getReadBuffer(void** buf, size_t* len) noexcept override {
    getReadBuffer_(buf, len);
  }

  MOCK_METHOD0(shouldOnlyNotify, bool());
  MOCK_METHOD1(onNotifyDataAvailable_, void(folly::AsyncUDPSocket&));
  void onNotifyDataAvailable(folly::AsyncUDPSocket& sock) noexcept override {
    onNotifyDataAvailable_(sock);
  }

  MOCK_METHOD4(
      onDataAvailable_,
      void(const folly::SocketAddress&, size_t, bool, OnDataAvailableParams));
  void onDataAvailable(
      const folly::SocketAddress& client,
      size_t len,
      bool truncated,
      OnDataAvailableParams params) noexcept override {
    onDataAvailable_(client, len, truncated, params);
  }

  MOCK_METHOD1(onReadError_, void(const folly::AsyncSocketException&));
  void onReadError(const folly::AsyncSocketException& ex) noexcept override {
    onReadError_(ex);
  }

  MOCK_METHOD0(onReadClosed_, void());
  void onReadClosed() noexcept override {
    onReadClosed_();
  }
};

class AsyncUDPSocketTest : public Test {
 public:
  void SetUp() override {
    socket_ = std::make_shared<AsyncUDPSocket>(&evb_);
    addr_ = folly::SocketAddress("127.0.0.1", 0);
    socket_->bind(addr_);
  }

  EventBase evb_;
  MockErrMessageCallback err;
  MockUDPReadCallback readCb;
  std::shared_ptr<AsyncUDPSocket> socket_;
  folly::SocketAddress addr_;
};

TEST_F(AsyncUDPSocketTest, TestConnect) {
  EXPECT_EQ(socket_->connect(addr_), 0);
}

TEST_F(AsyncUDPSocketTest, TestErrToNonExistentServer) {
  socket_->resumeRead(&readCb);
  socket_->setErrMessageCallback(&err);
  folly::SocketAddress addr("127.0.0.1", 10000);
  bool errRecvd = false;
#ifdef FOLLY_HAVE_MSG_ERRQUEUE
  EXPECT_CALL(err, errMessage_(_))
      .WillOnce(Invoke([this, &errRecvd](auto& cmsg) {
        if ((cmsg.cmsg_level == SOL_IP && cmsg.cmsg_type == IP_RECVERR) ||
            (cmsg.cmsg_level == SOL_IPV6 && cmsg.cmsg_type == IPV6_RECVERR)) {
          const struct sock_extended_err* serr =
              reinterpret_cast<const struct sock_extended_err*>(
                  CMSG_DATA(&cmsg));
          errRecvd =
              (serr->ee_origin == SO_EE_ORIGIN_ICMP || SO_EE_ORIGIN_ICMP6);
          LOG(ERROR) << "errno " << errnoStr(serr->ee_errno);
        }
        evb_.terminateLoopSoon();
      }));
#endif // FOLLY_HAVE_MSG_ERRQUEUE
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
  evb_.timer().scheduleTimeoutFn([&] { evb_.terminateLoopSoon(); },
                                 std::chrono::milliseconds(30));
  evb_.loopForever();
  EXPECT_TRUE(errRecvd);
}

TEST_F(AsyncUDPSocketTest, TestUnsetErrCallback) {
  socket_->resumeRead(&readCb);
  socket_->setErrMessageCallback(&err);
  socket_->setErrMessageCallback(nullptr);
  folly::SocketAddress addr("127.0.0.1", 10000);
  EXPECT_CALL(err, errMessage_(_)).Times(0);
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
  evb_.timer().scheduleTimeoutFn(
      [&] { evb_.terminateLoopSoon(); }, std::chrono::milliseconds(30));
  evb_.loopForever();
}

TEST_F(AsyncUDPSocketTest, CloseInErrorCallback) {
  socket_->resumeRead(&readCb);
  socket_->setErrMessageCallback(&err);
  folly::SocketAddress addr("127.0.0.1", 10000);
  bool errRecvd = false;
  EXPECT_CALL(err, errMessage_(_)).WillOnce(Invoke([this, &errRecvd](auto&) {
    errRecvd = true;
    socket_->close();
    evb_.terminateLoopSoon();
  }));
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
  evb_.timer().scheduleTimeoutFn([&] { evb_.terminateLoopSoon(); },
                                 std::chrono::milliseconds(30));
  evb_.loopForever();
  EXPECT_TRUE(errRecvd);
}

TEST_F(AsyncUDPSocketTest, TestNonExistentServerNoErrCb) {
  socket_->resumeRead(&readCb);
  folly::SocketAddress addr("127.0.0.1", 10000);
  bool errRecvd = false;
  folly::IOBufQueue readBuf;
  EXPECT_CALL(readCb, getReadBuffer_(_, _))
      .WillRepeatedly(Invoke([&readBuf](void** buf, size_t* len) {
        auto readSpace = readBuf.preallocate(2000, 10000);
        *buf = readSpace.first;
        *len = readSpace.second;
      }));
  ON_CALL(readCb, onReadError_(_)).WillByDefault(Invoke([&errRecvd](auto& ex) {
    LOG(ERROR) << ex.what();
    errRecvd = true;
  }));
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
  evb_.timer().scheduleTimeoutFn(
      [&] { evb_.terminateLoopSoon(); }, std::chrono::milliseconds(30));
  evb_.loopForever();
  EXPECT_FALSE(errRecvd);
}

TEST_F(AsyncUDPSocketTest, TestBound) {
  AsyncUDPSocket socket(&evb_);
  EXPECT_FALSE(socket.isBound());
  folly::SocketAddress address("0.0.0.0", 0);
  socket.bind(address);
  EXPECT_TRUE(socket.isBound());
}

TEST_F(AsyncUDPSocketTest, TestBoundUnixSocket) {
  folly::test::TemporaryDirectory tmpDirectory;
  const auto kTmpUnixSocketPath{tmpDirectory.path() / "unix_socket_path"};
  AsyncUDPSocket socket(&evb_);
  EXPECT_FALSE(socket.isBound());
  socket.bind(folly::SocketAddress::makeFromPath(kTmpUnixSocketPath.string()));
  EXPECT_TRUE(socket.isBound());
  socket.close();
}

TEST_F(AsyncUDPSocketTest, TestAttachAfterDetachEvbWithReadCallback) {
  socket_->resumeRead(&readCb);
  EXPECT_TRUE(socket_->isHandlerRegistered());
  socket_->detachEventBase();
  EXPECT_FALSE(socket_->isHandlerRegistered());
  socket_->attachEventBase(&evb_);
  EXPECT_TRUE(socket_->isHandlerRegistered());
}

TEST_F(AsyncUDPSocketTest, TestAttachAfterDetachEvbNoReadCallback) {
  EXPECT_FALSE(socket_->isHandlerRegistered());
  socket_->detachEventBase();
  EXPECT_FALSE(socket_->isHandlerRegistered());
  socket_->attachEventBase(&evb_);
  EXPECT_FALSE(socket_->isHandlerRegistered());
}

TEST_F(AsyncUDPSocketTest, TestDetachAttach) {
  folly::EventBase evb2;
  auto writeSocket = std::make_shared<folly::AsyncUDPSocket>(&evb_);
  folly::SocketAddress address("127.0.0.1", 0);
  writeSocket->bind(address);
  std::array<uint8_t, 1024> data;
  std::atomic<int> packetsRecvd{0};
  EXPECT_CALL(readCb, getReadBuffer_(_, _))
      .WillRepeatedly(Invoke([&](void** buf, size_t* len) {
        *buf = data.data();
        *len = 1024;
      }));
  EXPECT_CALL(readCb, onDataAvailable_(_, _, _, _))
      .WillRepeatedly(Invoke([&](const folly::SocketAddress&,
                                 size_t,
                                 bool,
                                 OnDataAvailableParams) { packetsRecvd++; }));
  socket_->resumeRead(&readCb);
  writeSocket->write(socket_->address(), folly::IOBuf::copyBuffer("hello"));
  while (packetsRecvd != 1) {
    evb_.loopOnce();
  }
  EXPECT_EQ(packetsRecvd, 1);

  socket_->detachEventBase();
  std::thread t([&] { evb2.loopForever(); });
  evb2.runInEventBaseThreadAndWait([&] { socket_->attachEventBase(&evb2); });
  writeSocket->write(socket_->address(), folly::IOBuf::copyBuffer("hello"));
  auto now = std::chrono::steady_clock::now();
  while (packetsRecvd != 2 ||
         std::chrono::steady_clock::now() <
             now + std::chrono::milliseconds(10)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  evb2.runInEventBaseThread([&] {
    socket_ = nullptr;
    evb2.terminateLoopSoon();
  });
  t.join();
  EXPECT_EQ(packetsRecvd, 2);
}
