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

#include <numeric>
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
using namespace testing;

using SizeVec = std::vector<size_t>;
using IOBufVec = std::vector<std::unique_ptr<folly::IOBuf>>;

struct TestData {
  explicit TestData(const SizeVec& in) : in_(in) {}

  bool checkOut() const {
    return (outNum_ == in_.size());
  }

  char getCharAt(size_t pos) {
    if (pos < in_.size()) {
      return static_cast<char>(in_[pos] % 256);
    }

    return 0;
  }

  bool appendOut(const char* data, size_t len) {
    outNum_++;
    if (outNum_ == in_.size()) {
      return true;
    }
    // check the size
    CHECK_EQ(len, in_[outNum_ - 1]);

    // check the payload
    char c = getCharAt(outNum_ - 1);
    for (size_t i = 0; i < len; i++) {
      CHECK_EQ(data[i], c);
    }

    return false;
  }

  IOBufVec getInBufs() {
    if (!in_.size()) {
      return IOBufVec();
    }

    IOBufVec ret;
    for (size_t i = 0; i < in_.size(); i++) {
      std::string str(in_[i], getCharAt(i));
      std::unique_ptr<folly::IOBuf> buf =
          folly::IOBuf::copyBuffer(str.data(), str.size());

      ret.emplace_back(std::move(buf));
    }

    return ret;
  }

  SizeVec in_;
  size_t outNum_{0};
  bool check_{true};
};

class UDPAcceptor : public AsyncUDPServerSocket::Callback {
 public:
  UDPAcceptor(EventBase* evb) : evb_(evb) {}

  void onListenStarted() noexcept override {}

  void onListenStopped() noexcept override {}

  void onDataAvailable(
      std::shared_ptr<folly::AsyncUDPSocket> socket,
      const folly::SocketAddress& client,
      std::unique_ptr<folly::IOBuf> data,
      bool /*unused*/,
      OnDataAvailableParams /*unused*/) noexcept override {
    // send pong
    socket->write(client, data->clone());
  }

 private:
  EventBase* const evb_{nullptr};
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
      acceptors_.emplace_back(&evb);

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
  explicit UDPClient(EventBase* evb, TestData& testData)
      : AsyncTimeout(evb), evb_(evb), testData_(testData) {}

  void start(const folly::SocketAddress& server) {
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

    // Start playing ping pong
    sendPing();
  }

  void connect() {
    socket_->connect(*connectAddr_);
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
    scheduleTimeout(5);
    auto bufs = testData_.getInBufs();
    writePing(bufs);
  }

  virtual void writePing(IOBufVec& bufs) {
    socket_->writem(
        folly::range(&server_, &server_ + 1), bufs.data(), bufs.size());
  }

  void getReadBuffer(void** buf, size_t* len) noexcept override {
    *buf = buf_;
    *len = sizeof(buf_);
  }

  void onDataAvailable(
      const folly::SocketAddress& /*unused*/,
      size_t len,
      bool /*unused*/,
      OnDataAvailableParams /*unused*/) noexcept override {
    VLOG(0) << "Got " << len << " bytes";
    if (testData_.appendOut(buf_, len)) {
      shutdown();
    }
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
    shutdown();
  }

  AsyncUDPSocket& getSocket() {
    return *socket_;
  }

  void setShouldConnect(const folly::SocketAddress& connectAddr) {
    connectAddr_ = connectAddr;
  }

 protected:
  folly::Optional<folly::SocketAddress> connectAddr_;
  EventBase* const evb_{nullptr};

  folly::SocketAddress server_;
  std::unique_ptr<AsyncUDPSocket> socket_;

 private:
  char buf_[2048];
  TestData& testData_;
};

class AsyncSocketSendmmsgIntegrationTest : public Test {
 public:
  void SetUp() override {
    server = std::make_unique<UDPServer>(
        &sevb, folly::SocketAddress("127.0.0.1", 0), 1);

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
      TestData& testData,
      folly::Optional<folly::SocketAddress> connectedAddress);

  folly::EventBase sevb;
  folly::EventBase cevb;
  TestData* testData_{nullptr};
  std::unique_ptr<std::thread> serverThread;
  std::unique_ptr<UDPServer> server;
  std::unique_ptr<UDPClient> client;
};

std::unique_ptr<UDPClient>
AsyncSocketSendmmsgIntegrationTest::performPingPongTest(
    TestData& testData,
    folly::Optional<folly::SocketAddress> connectedAddress) {
  testData_ = &testData;

  client = std::make_unique<UDPClient>(&cevb, testData);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress);
  }

  // Start event loop in a separate thread
  auto clientThread = std::thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(server->address()); });

  // Wait for client to finish
  clientThread.join();
  return std::move(client);
}

TEST_F(AsyncSocketSendmmsgIntegrationTest, PingPongRequest) {
  SizeVec in{1,   2,   3,   4,   5,   8,   8,   9,   10,  11,
             22,  33,  44,  55,  66,  77,  88,  99,  110, 120,
             220, 320, 420, 520, 620, 720, 820, 920, 1020};

  TestData testData(in);
  startServer();
  auto pingClient = performPingPongTest(testData, folly::none);
  CHECK(testData.checkOut());
}
