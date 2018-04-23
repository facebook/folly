/*
 * Copyright 2014-present Facebook, Inc.
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

#include <thread>

#include <folly/Conv.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using folly::AsyncTimeout;
using folly::AsyncUDPServerSocket;
using folly::AsyncUDPSocket;
using folly::EventBase;
using folly::IOBuf;
using folly::SocketAddress;
using namespace testing;

class UDPAcceptor : public AsyncUDPServerSocket::Callback {
 public:
  UDPAcceptor(EventBase* evb, int n) : evb_(evb), n_(n) {}

  void onListenStarted() noexcept override {}

  void onListenStopped() noexcept override {}

  void onDataAvailable(
      std::shared_ptr<folly::AsyncUDPSocket> /* socket */,
      const folly::SocketAddress& client,
      std::unique_ptr<folly::IOBuf> data,
      bool truncated) noexcept override {
    lastClient_ = client;
    lastMsg_ = data->moveToFbString().toStdString();

    auto len = data->computeChainDataLength();
    VLOG(4) << "Worker " << n_ << " read " << len << " bytes "
            << "(trun:" << truncated << ") from " << client.describe() << " - "
            << lastMsg_;

    sendPong();
  }

  void sendPong() noexcept {
    try {
      AsyncUDPSocket socket(evb_);
      socket.bind(folly::SocketAddress("127.0.0.1", 0));
      socket.write(lastClient_, folly::IOBuf::copyBuffer(lastMsg_));
    } catch (const std::exception& ex) {
      VLOG(4) << "Failed to send PONG " << ex.what();
    }
  }

 private:
  EventBase* const evb_{nullptr};
  const int n_{-1};

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
      acceptors_.emplace_back(&evb, i);

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

 private:
  EventBase* const evb_{nullptr};
  const folly::SocketAddress addr_;

  std::unique_ptr<AsyncUDPServerSocket> socket_;
  std::vector<std::thread> threads_;
  std::vector<folly::EventBase> evbs_;
  std::vector<UDPAcceptor> acceptors_;
};

class UDPClient : private AsyncUDPSocket::ReadCallback, private AsyncTimeout {
 public:
  explicit UDPClient(EventBase* evb) : AsyncTimeout(evb), evb_(evb) {}

  void start(const folly::SocketAddress& server, int n) {
    CHECK(evb_->isInEventBaseThread());

    server_ = server;
    socket_ = std::make_unique<AsyncUDPSocket>(evb_);

    try {
      socket_->bind(folly::SocketAddress("127.0.0.1", 0));
      VLOG(4) << "Client bound to " << socket_->address().describe();
    } catch (const std::exception& ex) {
      LOG(FATAL) << ex.what();
    }

    socket_->resumeRead(this);

    n_ = n;

    // Start playing ping pong
    sendPing();
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
    socket_->write(
        server_, folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
  }

  void getReadBuffer(void** buf, size_t* len) noexcept override {
    *buf = buf_;
    *len = 1024;
  }

  void onDataAvailable(
      const folly::SocketAddress& client,
      size_t len,
      bool truncated) noexcept override {
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

 private:
  EventBase* const evb_{nullptr};

  folly::SocketAddress server_;
  std::unique_ptr<AsyncUDPSocket> socket_;

  int pongRecvd_{0};

  int n_{0};
  char buf_[1024];
};

TEST(AsyncSocketTest, PingPong) {
  folly::EventBase sevb;
  UDPServer server(&sevb, folly::SocketAddress("127.0.0.1", 0), 4);

  // Start event loop in a separate thread
  auto serverThread = std::thread([&sevb]() { sevb.loopForever(); });

  // Wait for event loop to start
  sevb.waitUntilRunning();

  // Start the server
  sevb.runInEventBaseThreadAndWait([&]() { server.start(); });

  folly::EventBase cevb;
  UDPClient client(&cevb);

  // Start event loop in a separate thread
  auto clientThread = std::thread([&cevb]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client.start(server.address(), 1000); });

  // Wait for client to finish
  clientThread.join();

  // Check that some PING/PONGS were exchanged. Out of 1000 transactions
  // at least 1 should succeed
  CHECK_GT(client.pongRecvd(), 0);

  // Shutdown server
  sevb.runInEventBaseThread([&]() {
    server.shutdown();
    sevb.terminateLoopSoon();
  });

  // Wait for server thread to joib
  serverThread.join();
}

class TestAsyncUDPSocket : public AsyncUDPSocket {
 public:
  explicit TestAsyncUDPSocket(EventBase* evb) : AsyncUDPSocket(evb) {}

  MOCK_METHOD3(sendmsg, ssize_t(int, const struct msghdr*, int));
};

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

  MOCK_METHOD3(
      onDataAvailable_,
      void(const folly::SocketAddress&, size_t, bool));
  void onDataAvailable(
      const folly::SocketAddress& client,
      size_t len,
      bool truncated) noexcept override {
    onDataAvailable_(client, len, truncated);
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

TEST_F(AsyncUDPSocketTest, TestErrToNonExistentServer) {
  socket_->resumeRead(&readCb);
  socket_->setErrMessageCallback(&err);
  folly::SocketAddress addr("127.0.0.1", 10000);
  bool errRecvd = false;
  EXPECT_CALL(err, errMessage_(_))
      .WillOnce(Invoke([this, &errRecvd](auto& cmsg) {
        if ((cmsg.cmsg_level == SOL_IP && cmsg.cmsg_type == IP_RECVERR) ||
            (cmsg.cmsg_level == SOL_IPV6 && cmsg.cmsg_type == IPV6_RECVERR)) {
          const struct sock_extended_err* serr =
              reinterpret_cast<const struct sock_extended_err*>(
                  CMSG_DATA(&cmsg));
          errRecvd =
              (serr->ee_origin == SO_EE_ORIGIN_ICMP || SO_EE_ORIGIN_ICMP6);
          LOG(ERROR) << "errno " << strerror(serr->ee_errno);
        }
        evb_.terminateLoopSoon();
      }));
  socket_->write(addr, folly::IOBuf::copyBuffer("hey"));
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
