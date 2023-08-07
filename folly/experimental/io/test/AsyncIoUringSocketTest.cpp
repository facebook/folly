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
#include <random>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/Subprocess.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/io/AsyncIoUringSocket.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/IoUringEvent.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>
#include <folly/system/Shell.h>
#include <folly/test/SocketAddressTestHelper.h>

namespace folly {

namespace {

static constexpr std::chrono::milliseconds kTimeout{30000};
static constexpr size_t kBufferSize{1024};

std::string toString(std::unique_ptr<IOBuf> const& buf) {
  if (!buf) {
    return std::string();
  }
  auto coalesced = buf->coalesce();
  return std::string((char const*)coalesced.data(), coalesced.size());
}

class NullWriteCallback : public AsyncWriter::WriteCallback {
 public:
  void writeSuccess() noexcept override {}

  void writeErr(
      size_t bytesWritten, const AsyncSocketException& ex) noexcept override {
    LOG(FATAL) << "writeErr wrote=" << (int)bytesWritten << " " << ex;
  }
};

static NullWriteCallback nullWriteCallback;

class FutureWriteCallback : public AsyncWriter::WriteCallback {
 public:
  void writeSuccess() noexcept override {
    promiseContract.first.setValue(Unit{});
  }

  void writeErr(
      size_t bytesWritten, const AsyncSocketException& ex) noexcept override {
    promiseContract.first.setValue(
        makeUnexpected(std::make_pair(bytesWritten, ex)));
  }

  using TResult = Expected<Unit, std::pair<size_t, AsyncSocketException>>;
  std::pair<Promise<TResult>, SemiFuture<TResult>> promiseContract =
      makePromiseContract<TResult>();
};

} // namespace

class EchoTransport : public AsyncReader::ReadCallback,
                      public AsyncWriter::WriteCallback {
 public:
  explicit EchoTransport(AsyncSocketTransport::UniquePtr s, bool bm)
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
    // have to copy as buff will be reused after
    transport->writeChain(this, IOBuf::copyBuffer(buff.data(), len));
  }

  bool isBufferMovable() noexcept override { return bufferMovable; }

  void readBufferAvailable(std::unique_ptr<IOBuf> readBuf) noexcept override {
    VLOG(1) << "readBuffer available " << readBuf->computeChainDataLength();
    transport->writeChain(this, std::move(readBuf));
  }

  AsyncSocketTransport::UniquePtr transport;
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

    data += toString(readBuf);
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
  bool sendzc = false;
  bool registerFd = true;
  std::string testName() const {
    return folly::to<std::string>(
        ioUringServer ? "ioUringServer" : "oldServer",
        "_",
        ioUringClient ? "ioUringClient" : "oldClient",
        "_",
        manySmallBuffers ? "manySmallBuffers" : "oneBigBuffer",
        supportBufferMovable ? "" : "_noSupportBufferMovable",
        sendzc ? "_zerocopy" : "",
        "_",
        registerFd ? "" : "_noRegisterFd",
        epoll ? "epoll" : "iouring",
        "Backend");
  }
};

struct ConnectedOptions {
  std::string fastOpenInitial;
  bool serverShouldRead = true;

  ConnectedOptions withNoServerShouldRead() {
    auto ret = *this;
    ret.serverShouldRead = false;
    return ret;
  }

  ConnectedOptions withFastOpen(std::string i) {
    auto ret = *this;
    ret.fastOpenInitial = std::move(i);
    return ret;
  }
};

class AsyncIoUringSocketTest : public ::testing::TestWithParam<TestParams>,
                               public AsyncServerSocket::AcceptCallback,
                               public AsyncSocket::ConnectCallback {
 public:
  static IoUringBackend::Options ioOptions(TestParams const& p) {
    auto options =
        IoUringBackend::Options{}.setUseRegisteredFds(p.registerFd ? 64 : 0);
    if (p.manySmallBuffers) {
      options.setInitialProvidedBuffers(1024, 2000);
    } else {
      options.setInitialProvidedBuffers(2000000, 1);
    }
    options.setDeferTaskRun(true);
    return options;
  }

  EventBase::Options ioUringEbOptions() {
    return EventBase::Options{}.setBackendFactory(
        [p = GetParam()]() -> std::unique_ptr<EventBaseBackendBase> {
          return std::make_unique<IoUringBackend>(ioOptions(p));
        });
  }

  EventBase::Options ebOptions() {
    if (GetParam().epoll) {
      return {};
    }
    return ioUringEbOptions();
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
      backend = backendForSocketConstructor = &ioUringEvent->backend();
    } else {
      backend = dynamic_cast<IoUringBackend*>(base->getBackend());
    }

    backend->loopPoll(); // init delayed bits as this is the only thread

    serverSocket = AsyncServerSocket::newSocket(base.get());
    serverSocket->setTFOEnabled(true, 1);
    serverSocket->bind(0);
    serverSocket->listen(1024);
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

  AsyncIoUringSocket::Options ioUringSocketOptions() const {
    AsyncIoUringSocket::Options ret;
    if (GetParam().sendzc) {
      ret.zeroCopyEnable = [](auto&&) { return true; };
    }
    return ret;
  }

  Connected makeConnected(ConnectedOptions options = ConnectedOptions{}) {
    AsyncSocketTransport::UniquePtr client;
    if (GetParam().ioUringClient) {
      client = AsyncSocketTransport::UniquePtr(new AsyncIoUringSocket(
          base.get(), backendForSocketConstructor, ioUringSocketOptions()));
    } else {
      client =
          AsyncSocketTransport::UniquePtr(AsyncSocket::newSocket(base.get()));
    }

    if (options.fastOpenInitial.size()) {
      client->enableTFO();
    }
    client->connect(this, serverAddress);
    if (options.fastOpenInitial.size()) {
      client->writeChain(
          &nullWriteCallback, IOBuf::copyBuffer(options.fastOpenInitial));
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
              AsyncSocket::newSocket(base.get(), fd),
              backendForSocketConstructor,
              ioUringSocketOptions()))
        : AsyncTransport::UniquePtr(AsyncSocket::newSocket(base.get(), fd));
    if (options.serverShouldRead) {
      sock->setReadCB(cb.get());
    }
    return Connected{std::move(c), std::move(sock), std::move(cb)};
  }

  bool unableToRun = false;
  std::unique_ptr<EventBase> base;
  std::unique_ptr<IoUringEvent> ioUringEvent;
  std::shared_ptr<AsyncServerSocket> serverSocket;
  IoUringBackend* backendForSocketConstructor = nullptr;
  IoUringBackend* backend = nullptr;
  SocketAddress serverAddress;

  Promise<NetworkSocket> fdPromise;
};

#define MAYBE_SKIP()                   \
  if (unableToRun) {                   \
    LOG(INFO) << "Unsupported kernel"; \
    return;                            \
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

  AsyncIoUringSocket::UniquePtr socket(
      new AsyncIoUringSocket(base.get(), backend));
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

  auto c = makeConnected(ConnectedOptions{}.withNoServerShouldRead());
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

struct DetachCB : folly::AsyncDetachFdCallback {
  void fdDetached(
      NetworkSocket ns, std::unique_ptr<IOBuf> unread) noexcept override {
    promise.setValue(std::make_pair(ns, std::move(unread)));
  }

  void fdDetachFail(const AsyncSocketException& ex) noexcept override {
    promise.setException(ex);
  }

  folly::Promise<std::pair<NetworkSocket, std::unique_ptr<IOBuf>>> promise;
};

TEST_P(AsyncIoUringSocketTest, Detach) {
  MAYBE_SKIP();
  auto c = makeConnected();
  auto* was = c.server->getUnderlyingTransport<folly::AsyncIoUringSocket>();
  ASSERT_NE(was, nullptr);

  // write something
  c.client->transport->write(&nullWriteCallback, "hello", 5);
  // make sure it gets run
  backend->submitOutstanding();

  // sleep a bit to get the read all the way into the completion queue
  /* sleep override */ std::this_thread::sleep_for(
      std::chrono::milliseconds(5));

  // now detach before the write is completed
  DetachCB cb;
  was->asyncDetachFd(&cb);
  ASSERT_FALSE(cb.promise.isFulfilled()) << "must wait for read to finish";

  auto res = cb.promise.getSemiFuture()
                 .within(kTimeout)
                 .via(base.get())
                 .getVia(base.get());
  EXPECT_GE(res.first.toFd(), 0);
  if (res.second) {
    // did not cancel in time
    EXPECT_EQ("hello", toString(res.second));
  } else {
    // did cancel in time
    char buff[128];
    memset(buff, 0, sizeof(buff));
    int ret;
    do {
      ret = read(res.first.toFd(), &buff, sizeof(buff));
    } while (ret == -1 && errno == EINTR);
    ASSERT_EQ(5, ret);
    EXPECT_EQ("hello", std::string(buff));
  }
}

TEST_P(AsyncIoUringSocketTest, DetachEventBase) {
  MAYBE_SKIP();
  auto c = makeConnected();

  // write something
  FutureWriteCallback fwc;
  auto* transport = c.client->transport.get();
  transport->write(&fwc, "hello", 5);

  // make sure it gets run
  backend->submitOutstanding();
  ASSERT_TRUE(transport->isDetachable());
  transport->detachEventBase();

  EventBase newBase{ioUringEbOptions()};
  transport->attachEventBase(&newBase);

  auto resFut = c.callback->waitFor(5).via(folly::getGlobalCPUExecutor().get());
  auto start = std::chrono::steady_clock::now();
  do {
    newBase.loopOnce(EVLOOP_NONBLOCK);
    base->loopOnce(EVLOOP_NONBLOCK);
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds(20));
    auto res = resFut.poll();
    if (res) {
      EXPECT_EQ("hello", res->value());
      break;
    }
    if (std::chrono::steady_clock::now() > start + std::chrono::seconds(1)) {
      FAIL();
      break;
    }
  } while (true);

  // make sure write arrived, it should be on the new event base
  ASSERT_TRUE(std::move(fwc.promiseContract.second)
                  .via(&newBase)
                  .getVia(&newBase)
                  .hasValue());

  ASSERT_TRUE(transport->isDetachable());
  transport->detachEventBase();
  transport->attachEventBase(base.get());
}

TEST_P(AsyncIoUringSocketTest, DetachEventBaseClear) {
  MAYBE_SKIP();
  auto c = makeConnected();

  // write something
  c.client->transport->write(&nullWriteCallback, "hello", 5);

  backend->submitOutstanding();
  ASSERT_TRUE(c.client->transport->isDetachable());
  c.client->transport->detachEventBase();

  // now free things in the middle
  base->loopOnce(EVLOOP_NONBLOCK);
}

TEST_P(AsyncIoUringSocketTest, FastOpen) {
  MAYBE_SKIP();
  bool had_fastopen = false;
  bool can_fastopen = false;
  std::string fo;
  if (readFile("/proc/sys/net/ipv4/tcp_fastopen", fo)) {
    auto fast_open = folly::to<int>(fo);
    if (fast_open == 3) {
      can_fastopen = true;
    }
  }
  if (!can_fastopen) {
    LOG(INFO) << "/proc/sys/net/ipv4/tcp_fastopen must be 3 to do fastopen, "
                 "but we will test the code flow anyway";
  }
  // technically we could run ip tcp_metrics flush here, but messing with the
  // system in a test is awful
  folly::Subprocess subProc("/sbin/ip tcp_metrics show ::1"_shellify());
  int has_cookies_already = subProc.wait().exitStatus();
  if (has_cookies_already == 0) {
    LOG(INFO)
        << "already had cookies, so cannot do fastopen test, but will test code flow anyway. "
        << " you could do a `/sbin/ip tcp_metrics flush` to test this";
    had_fastopen = true;
  }
  auto opts = ConnectedOptions{}.withFastOpen("hello");
  {
    auto conn = makeConnected(opts);
    EXPECT_EQ(
        "hello", conn.callback->waitFor(5).via(base.get()).getVia(base.get()));
    if (!had_fastopen) {
      EXPECT_FALSE(conn.client->transport->getTFOSucceded());
    }
  }
  {
    auto conn = makeConnected(opts);
    EXPECT_EQ(
        "hello", conn.callback->waitFor(5).via(base.get()).getVia(base.get()));
    if (can_fastopen) {
      EXPECT_TRUE(conn.client->transport->getTFOSucceded());
    }
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
  for (int i = 0; i < 4096; i++) {
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

namespace {
std::string randomString(size_t n) {
  std::random_device r;
  std::default_random_engine e1(r());

  std::uniform_int_distribution<char> uniform_dist('A', 'Z');

  std::string ret;
  ret.reserve(n);
  for (size_t i = 0; i < n; i++) {
    ret.push_back(uniform_dist(e1));
  }
  return ret;
}
} // namespace

TEST_P(AsyncIoUringSocketTestAll, WriteBig) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  cb->setHoldData(true);
  std::string big = randomString(4000000);
  s->write(&nullWriteCallback, big.c_str(), big.size());
  auto res = cb->waitFor(big.size()).via(base.get()).getVia(base.get());
  EXPECT_TRUE(big == res) << big.size() << " vs " << res.size();
}

TEST_P(AsyncIoUringSocketTestAll, WriteBigChunked) {
  MAYBE_SKIP();
  auto [e, s, cb] = makeConnected();
  cb->setHoldData(true);
  std::string big = randomString(4000000);
  size_t at = 0;
  int const kChunkSize = 256;
  while (at < big.size()) {
    auto len = std::min<size_t>(big.size() - at, kChunkSize);
    s->write(&nullWriteCallback, big.c_str() + at, len);
    at += len;
  }
  auto res = cb->waitFor(big.size()).via(base.get()).getVia(base.get());
  EXPECT_TRUE(big == res) << big.size() << " vs " << res.size();
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
  if (!GetParam().ioUringServer) {
    // folly::AsyncSocket is not totally reliable with timeouts
    return;
  }
  auto conn = makeConnected(ConnectedOptions{}.withNoServerShouldRead());
  FutureWriteCallback ecb;
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
  ASSERT_TRUE(ex.hasError());
  EXPECT_EQ(AsyncSocketException::TIMED_OUT, ex.error().second.getType());
}

auto mkAllTestParams() {
  std::vector<TestParams> t;

  auto addFeatureCases = [&](TestParams const& base) {
    TestParams all = base;

    // add test cases where each feature is not the default, as well as one
    // where all the features are not the default
    auto add_flip_case = [&](auto ptr) {
      auto tc = base;
      tc.*ptr = all.*ptr = !(tc.*ptr);
      t.push_back(tc);
    };

    add_flip_case(&TestParams::registerFd);
    if (IoUringBackend::kernelSupportsSendZC()) {
      add_flip_case(&TestParams::sendzc);
    }
    add_flip_case(&TestParams::supportBufferMovable);
    t.push_back(all);
  };

  for (bool server : {false, true}) {
    for (bool client : {false, true}) {
      for (bool manySmallBuffers : {false, true}) {
        for (bool epoll : {false, true}) {
          TestParams base;
          base.ioUringServer = server;
          base.ioUringClient = client;
          base.manySmallBuffers = manySmallBuffers;
          base.epoll = epoll;
          t.push_back(base);

          // only expand feature flags in some cases to reduce the massive
          // explosion of tests
          if (server && client && !epoll) {
            addFeatureCases(base);
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
  auto conn = makeConnected(ConnectedOptions{}.withNoServerShouldRead());
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
