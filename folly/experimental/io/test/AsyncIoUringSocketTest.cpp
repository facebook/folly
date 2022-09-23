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

#include <array>
#include <chrono>
#include <map>
#include <vector>

#include <folly/experimental/io/AsyncIoUringSocket.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/IoUringEvent.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>
#include <folly/test/SocketAddressTestHelper.h>

namespace folly {

namespace {

static constexpr std::chrono::milliseconds kTimeout{4000};
static constexpr size_t kBufferSize{1024};

class NullWriteCallback : public AsyncWriter::WriteCallback {
 public:
  void writeSuccess() noexcept override {}

  void writeErr(
      size_t bytesWritten, const AsyncSocketException& ex) noexcept override {
    LOG(FATAL) << "writeErr wrote=" << (int)bytesWritten << " " << ex;
  }
};

static NullWriteCallback nullWriteCallback;

class ExpectErrorWriteCallback : public AsyncWriter::WriteCallback {
 public:
  void writeSuccess() noexcept override {
    promiseContract.first.setException<std::runtime_error>(
        std::runtime_error{"did not expect success"});
  }

  void writeErr(
      size_t bytesWritten, const AsyncSocketException& ex) noexcept override {
    promiseContract.first.setValue(std::make_pair(bytesWritten, ex));
  }

  std::pair<
      Promise<std::pair<size_t, AsyncSocketException>>,
      SemiFuture<std::pair<size_t, AsyncSocketException>>>
      promiseContract =
          makePromiseContract<std::pair<size_t, AsyncSocketException>>();
};

} // namespace

class EchoTransport : public AsyncReader::ReadCallback,
                      public AsyncWriter::WriteCallback {
 public:
  explicit EchoTransport(std::shared_ptr<AsyncTransport> s, bool bm)
      : transport(std::move(s)), bufferMovable(bm) {}

  void start() { transport->setReadCB(this); }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = buff.data();
    *lenReturn = buff.size();
  }

  void writeSuccess() noexcept override {}

  void writeErr(
      size_t bytesWritten, const AsyncSocketException& ex) noexcept override {
    LOG_EVERY_N(ERROR, 10000) << "writeErr " << bytesWritten << " " << ex;
    transport->close();
  }

  void readEOF() noexcept override {
    VLOG(1) << "Closing transport!";
    transport->close();
  }

  void readErr(const AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << "readErr " << ex;
    transport->close();
  }

  void readDataAvailable(size_t len) noexcept override {
    VLOG(1) << "readDataAvailable " << len;
    transport->write(this, buff.data(), len);
  }

  bool isBufferMovable() noexcept override { return bufferMovable; }

  void readBufferAvailable(std::unique_ptr<IOBuf> readBuf) noexcept override {
    VLOG(1) << "readBuffer available " << readBuf->computeChainDataLength();
    transport->writeChain(this, std::move(readBuf));
  }

  std::shared_ptr<AsyncTransport> transport;
  bool bufferMovable;
  std::array<char, kBufferSize> buff;
};

class CollectCallback : public AsyncReader::ReadCallback {
 public:
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = buff.data();
    *lenReturn = buff.size();
  }

  void readEOF() noexcept override { clear(); }

  void readErr(const AsyncSocketException& ex) noexcept override {
    if (promise) {
      promise->second.setException(ex);
    }
    clear();
  }

  void readDataAvailable(size_t len) noexcept override {
    VLOG(1) << "CollectCallback::readDataAvailable " << len;
    if (hadBuffers.has_value() && *hadBuffers) {
      LOG(FATAL) << "must either send buffers or use getReadBuffer";
    }
    hadBuffers = false;
    data += std::string(buff.data(), len);
    dataAvailable();
  }

  SemiFuture<std::string> waitFor(size_t n) {
    auto [p, f] = makePromiseContract<std::string>();
    promise = std::make_pair(n, std::move(p));
    return std::move(f).within(kTimeout);
  }

  void readBufferAvailable(std::unique_ptr<IOBuf> readBuf) noexcept override {
    if (hadBuffers.has_value() && !*hadBuffers) {
      LOG(FATAL) << "must either send buffers or use getReadBuffer";
    }
    hadBuffers = true;

    if (holdData) {
      auto cloned = readBuf->clone();
      if (bufData) {
        bufData->appendToChain(std::move(readBuf));
      } else {
        bufData = std::move(readBuf);
      }
      readBuf = std::move(cloned);
    }

    auto coalesced = readBuf->coalesce();
    VLOG(1) << "CollectCallback::readBufferAvailable " << coalesced.size();
    data += std::string((char const*)coalesced.data(), coalesced.size());
    dataAvailable();
  }

  void clear() {
    data.clear();
    promise.reset();
    bufData.reset();
  }

  void dataAvailable() {
    VLOG(1) << "CollectCallback::dataAvailable have=" << data.size()
            << " want=" << (promise ? static_cast<int>(promise->first) : -1);
    if (!promise || promise->first > data.size()) {
      return;
    }
    promise->second.setValue(data.substr(0, promise->first));
    data.erase(0, promise->first);
    promise.reset();
  }

  bool isBufferMovable() noexcept override { return true; }

  void setHoldData(bool b) { holdData = b; }

  std::optional<std::pair<size_t, Promise<std::string>>> promise;
  std::shared_ptr<AsyncSocket> sock;
  std::string data;
  std::unique_ptr<IOBuf> bufData;
  std::optional<bool> hadBuffers;
  std::array<char, kBufferSize> buff;
  bool holdData = true;
};

struct TestParams {
  bool ioUringServer = false;
  bool ioUringClient = false;
  bool manySmallBuffers = false;
  bool epoll = false;
  bool supportBufferMovable = true;
  std::string testName() const {
    return folly::to<std::string>(
        ioUringServer ? "ioUringServer" : "oldServer",
        "_",
        ioUringClient ? "ioUringClient" : "oldClient",
        "_",
        manySmallBuffers ? "manySmallBuffers" : "oneBigBuffer",
        "_",
        supportBufferMovable ? "" : "noSupportBufferMovable",
        "_",
        epoll ? "epoll" : "iouring",
        "Backend");
  }
};

class AsyncIoUringSocketTest : public ::testing::TestWithParam<TestParams>,
                               public AsyncServerSocket::AcceptCallback,
                               public AsyncSocket::ConnectCallback {
 public:
  static IoUringBackend::Options ioOptions(TestParams const& p) {
    auto options = IoUringBackend::Options{}.setUseRegisteredFds(64);
    if (p.manySmallBuffers) {
      options.setInitialProvidedBuffers(1024, 2000);
    } else {
      options.setInitialProvidedBuffers(2000000, 1);
    }
    return options;
  }

  EventBase::Options ebOptions() {
    if (GetParam().epoll) {
      return {};
    }
    return EventBase::Options{}.setBackendFactory(
        [p = GetParam()]() -> std::unique_ptr<EventBaseBackendBase> {
          return std::make_unique<IoUringBackend>(ioOptions(p));
        });
  }

  void maybeSkip() {
    if (unableToRun) {
      GTEST_SKIP();
    }
  }

  AsyncIoUringSocketTest() {
    try {
      base = std::make_unique<EventBase>(ebOptions());
    } catch (IoUringBackend::NotAvailable const&) {
      unableToRun = true;
      return;
    }

    if (GetParam().epoll) {
      try {
        ioUringEvent = std::make_unique<IoUringEvent>(
            base.get(), ioOptions(GetParam()), true);
      } catch (IoUringBackend::NotAvailable const&) {
        unableToRun = true;
        return;
      }
      backend = &ioUringEvent->backend();
    }

    serverSocket = AsyncServerSocket::newSocket(base.get());
    serverSocket->bind(0);
    serverSocket->listen(0);
    serverSocket->addAcceptCallback(this, nullptr);
    serverSocket->startAccepting();
    serverSocket->getAddress(&serverAddress);
  }

  void connectionAccepted(
      NetworkSocket ns, const SocketAddress&, AcceptInfo) noexcept override {
    fdPromise.setValue(ns);
  }

  void connectSuccess() noexcept override {}
  void connectErr(const AsyncSocketException& ex) noexcept override {
    LOG(FATAL) << ex;
  }

  struct Connected {
    std::unique_ptr<EchoTransport> client;
    AsyncTransport::UniquePtr server;
    std::unique_ptr<CollectCallback> callback;
    ~Connected() {
      if (server) {
        server->setReadCB(nullptr);
      }
    }
  };
  Connected makeConnected(bool server_should_read = true) {
    std::shared_ptr<AsyncTransport> client;
    if (GetParam().ioUringClient) {
      auto c = std::make_shared<AsyncIoUringSocket>(base.get(), backend);
      c->connect(this, serverAddress);
      client = c;
    } else {
      auto c = AsyncSocket::newSocket(base.get());
      c->connect(this, serverAddress);
      client = std::move(c);
    }
    auto fd = fdPromise.getFuture()
                  .within(kTimeout)
                  .via(base.get())
                  .getVia(base.get());
    fdPromise = {};
    auto c = std::make_unique<EchoTransport>(
        std::move(client), GetParam().supportBufferMovable);
    c->start();
    auto cb = std::make_unique<CollectCallback>();
    AsyncTransport::UniquePtr sock = GetParam().ioUringServer
        ? AsyncTransport::UniquePtr(new AsyncIoUringSocket(
              AsyncSocket::newSocket(base.get(), fd), backend))
        : AsyncTransport::UniquePtr(AsyncSocket::newSocket(base.get(), fd));
    if (server_should_read) {
      sock->setReadCB(cb.get());
    }
    return Connected{std::move(c), std::move(sock), std::move(cb)};
  }

  bool unableToRun = false;
  std::unique_ptr<EventBase> base;
  std::unique_ptr<IoUringEvent> ioUringEvent;
  std::shared_ptr<AsyncServerSocket> serverSocket;
  IoUringBackend* backend = nullptr;
  SocketAddress serverAddress;

  Promise<NetworkSocket> fdPromise;
};

#define MAYBE_SKIP()                      \
  if (unableToRun) {                      \
    GTEST_SKIP() << "Unsupported kernel"; \
    return;                               \
  }

TEST_P(AsyncIoUringSocketTest, ConnectTimeout) {
  MAYBE_SKIP();
  struct CB : AsyncSocket::ConnectCallback {
    void connectSuccess() noexcept override {
      prom.setValue(makeExpected<AsyncSocketException>(Unit{}));
    }
    void connectErr(const AsyncSocketException& ex) noexcept override {
      prom.setValue(makeUnexpected(ex));
    }
    Promise<Expected<Unit, AsyncSocketException>> prom;
  } cb;

  // Try connecting to server that won't respond.
  //
  // This depends somewhat on the network where this test is run.
  // Hopefully this IP will be routable but unresponsive.
  // (Alternatively, we could try listening on a local raw socket, but that
  // normally requires root privileges.)
  auto host = SocketAddressTestHelper::isIPv6Enabled()
      ? SocketAddressTestHelper::kGooglePublicDnsAAddrIPv6
      : SocketAddressTestHelper::isIPv4Enabled()
      ? SocketAddressTestHelper::kGooglePublicDnsAAddrIPv4
      : nullptr;

  auto socket = std::make_shared<AsyncIoUringSocket>(base.get(), backend);
  socket->connect(
      &cb, SocketAddress{host, 65535}, std::chrono::milliseconds(1));

  auto res = cb.prom.getSemiFuture()
                 .within(kTimeout)
                 .via(base.get())
                 .getVia(base.get());
  ASSERT_FALSE(res);
  if (res.error().getType() == AsyncSocketException::NOT_OPEN) {
    // This can happen if we could not route to the IP address picked above.
    // In this case the connect will fail immediately rather than timing out.
    // Just skip the test in this case.
    GTEST_SKIP() << "do not have a routable but unreachable IP address";
    return;
  }
  EXPECT_EQ(res.error().getType(), AsyncSocketException::TIMED_OUT)
      << res.error().what();
}

TEST_P(AsyncIoUringSocketTest, EoF) {
  MAYBE_SKIP();
  struct CB : AsyncReader::ReadCallback {
    void readDataAvailable(size_t) noexcept override {
      // will terminate...
      terminate_with<std::runtime_error>("unexpected data");
    }
    void readEOF() noexcept override {
      VLOG(1) << "CB Setting EOF";
      prom.setValue(makeExpected<AsyncSocketException>(Unit{}));
    }

    void readErr(const AsyncSocketException& ex) noexcept override {
      VLOG(1) << "CB Setting Err " << ex;
      prom.setValue(makeUnexpected(ex));
    }

    void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
      *bufReturn = &buff;
      *lenReturn = 1;
    }

    Promise<Expected<Unit, AsyncSocketException>> prom;
    char buff;
  };

  auto c = makeConnected(false);
  {
    CB cb_eof;
    c.server->setReadCB(&cb_eof);
    c.client->transport->closeNow();
    c.client.reset();
    EXPECT_TRUE(cb_eof.prom.getSemiFuture()
                    .within(kTimeout)
                    .via(base.get())
                    .getVia(base.get())
                    .hasValue());
    c.server->setReadCB(nullptr);
  }
  EXPECT_FALSE(c.server->good());
  {
    CB cb_invalid;
    c.server->setReadCB(&cb_invalid);
    auto ex = cb_invalid.prom.getSemiFuture()
                  .within(kTimeout)
                  .via(base.get())
                  .getVia(base.get());
    ASSERT_TRUE(ex.hasError());
    auto er = ex.error();
    EXPECT_EQ(AsyncSocketException::NOT_OPEN, er.getType());
    c.server->setReadCB(nullptr);
  }
}

class AsyncIoUringSocketTestAll : public AsyncIoUringSocketTest {};

TEST_P(AsyncIoUringSocketTestAll, WriteChain2) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  s->writeChain(&nullWriteCallback, IOBuf::copyBuffer("hello"));
  EXPECT_EQ("hello", cb->waitFor(5).via(base.get()).getVia(base.get()));
  s->writeChain(&nullWriteCallback, IOBuf::copyBuffer("there"));
  EXPECT_EQ("there", cb->waitFor(5).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, WriteChainOrder) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  auto chain = IOBuf::copyBuffer("h");
  chain->appendToChain(IOBuf::copyBuffer("e"));
  chain->appendToChain(IOBuf::copyBuffer("ll"));
  chain->appendToChain(IOBuf::copyBuffer("o"));
  s->writeChain(&nullWriteCallback, std::move(chain));
  EXPECT_EQ("hello", cb->waitFor(5).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, WriteChainLong) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  auto chain = IOBuf::copyBuffer("?");
  std::string res = "?";
  for (int i = 0; i < 512; i++) {
    std::string x(1, 'a' + i % 26);
    chain->appendToChain(IOBuf::copyBuffer(x));
    res += x;
  }
  s->writeChain(&nullWriteCallback, std::move(chain));
  EXPECT_EQ(res, cb->waitFor(res.size()).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, Write) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  s->write(&nullWriteCallback, "hello", 5);
  EXPECT_EQ("hello", cb->waitFor(5).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, WriteAfterWait) {
  MAYBE_SKIP();
  auto conn = makeConnected();
  auto& s = conn.server;
  auto& cb = conn.callback;

  EXPECT_EQ(
      "hello",
      folly::futures::sleep(std::chrono::milliseconds(500))
          .via(base.get())
          .thenValue([&](auto&&) { s->write(&nullWriteCallback, "hello", 5); })
          .thenValue([&](auto&&) { return cb->waitFor(5); })
          .getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, WriteBig) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  cb->setHoldData(true);
  std::string big(4000000, 'X');
  s->write(&nullWriteCallback, big.c_str(), big.size());
  EXPECT_EQ(big, cb->waitFor(big.size()).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, WriteBigDrop) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  cb->setHoldData(false); // should trigger overflow in provided buffers
  std::string big(4000000, 'X');
  s->write(&nullWriteCallback, big.c_str(), big.size());
  EXPECT_EQ(big, cb->waitFor(big.size()).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, Writev) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  std::array<iovec, 2> iov = {{{(void*)"hel", 3}, {(void*)"lo", 2}}};
  s->writev(&nullWriteCallback, iov.data(), iov.size());
  EXPECT_EQ("hello", cb->waitFor(5).via(base.get()).getVia(base.get()));
}

TEST_P(AsyncIoUringSocketTestAll, SendTimeout) {
  MAYBE_SKIP();
  if (!GetParam().ioUringClient) {
    return;
  }
  auto conn = makeConnected(false);
  ExpectErrorWriteCallback ecb;
  std::string big(40000000, 'X');
  std::vector<iovec> iov;
  iov.resize(100);
  for (size_t i = 0; i < iov.size(); i++) {
    iov[i].iov_base = big.data();
    iov[i].iov_len = big.size();
  }
  base->runInEventBaseThread([&]() {
    conn.server->setSendTimeout(1);
    conn.server->writev(&ecb, iov.data(), iov.size());
  });
  auto ex =
      std::move(ecb.promiseContract.second).via(base.get()).getVia(base.get());
  EXPECT_EQ(AsyncSocketException::TIMED_OUT, ex.second.getType());
}

auto mkAllTestParams() {
  std::vector<TestParams> t;
  for (bool server : {false, true}) {
    for (bool client : {false, true}) {
      for (bool manySmallBuffers : {false, true}) {
        for (bool epoll : {false, true}) {
          for (bool bm : {false, true}) {
            TestParams base;
            base.ioUringServer = server;
            base.ioUringClient = client;
            base.manySmallBuffers = manySmallBuffers;
            base.epoll = epoll;
            base.supportBufferMovable = bm;
            t.push_back(base);
          }
        }
      }
    }
  }
  return t;
}

INSTANTIATE_TEST_SUITE_P(
    AsyncIoUringSocketTest,
    AsyncIoUringSocketTestAll,
    ::testing::ValuesIn(mkAllTestParams()),
    [](const ::testing::TestParamInfo<TestParams>& info) {
      return info.param.testName();
    });

TestParams mkBasicTestParams() {
  TestParams t;
  t.ioUringClient = t.ioUringServer = true;
  return t;
}

INSTANTIATE_TEST_SUITE_P(
    AsyncIoUringSocketTest,
    AsyncIoUringSocketTest,
    ::testing::Values(mkBasicTestParams()),
    [](const ::testing::TestParamInfo<TestParams>& info) {
      return info.param.testName();
    });

class AsyncIoUringSocketTakeoverTest : public AsyncIoUringSocketTest {};

class AsyncSocketWithPreRead : public AsyncSocket {
 public:
  AsyncSocketWithPreRead(AsyncSocket::UniquePtr a, std::string const& pre_read)
      : AsyncSocket(std::move(a)) {
    preReceivedData_ = IOBuf::copyBuffer(pre_read);
  }
};

TEST_P(AsyncIoUringSocketTakeoverTest, PreRead) {
  MAYBE_SKIP();
  auto conn = makeConnected(false);
  AsyncSocket::UniquePtr sock(
      dynamic_cast<AsyncSocket*>(conn.server.release()));
  ASSERT_NE(sock, nullptr);
  AsyncIoUringSocket::UniquePtr io_uring(
      new AsyncIoUringSocket(AsyncSocket::UniquePtr(
          new AsyncSocketWithPreRead(std::move(sock), "hello"))));
  io_uring->setReadCB(conn.callback.get());
  io_uring->write(&nullWriteCallback, "there", 5);
  EXPECT_EQ(
      "hellothere",
      conn.callback->waitFor(10).via(base.get()).getVia(base.get()));
}

TestParams mkTakeoverParams() {
  TestParams t;
  t.ioUringClient = t.ioUringServer = false;
  return t;
}

INSTANTIATE_TEST_SUITE_P(
    AsyncIoUringSocketTakeoverTest,
    AsyncIoUringSocketTakeoverTest,
    ::testing::Values(mkTakeoverParams()),
    [](const ::testing::TestParamInfo<TestParams>& info) {
      return info.param.testName();
    });

} // namespace folly
