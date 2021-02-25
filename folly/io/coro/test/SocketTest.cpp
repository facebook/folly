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

#include <folly/Portability.h>

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <folly/io/async/test/ScopedBoundPort.h>
#include <folly/io/coro/ServerSocket.h>
#include <folly/io/coro/Socket.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace std::chrono_literals;
using namespace folly;
using namespace folly::coro;

class SocketTest : public testing::Test {
 public:
  template <typename F>
  void run(F f) {
    blockingWait(co_invoke(std::move(f)), &evb);
  }

  folly::coro::Task<> requestCancellation() {
    cancelSource.requestCancellation();
    co_return;
  }

  EventBase evb;
  CancellationSource cancelSource;
};

class ServerSocketTest : public SocketTest {
 public:
  folly::coro::Task<Socket> connect() {
    co_return co_await Socket::connect(&evb, srv.getAddress(), 0ms);
  }
  TestServer srv;
};

TEST_F(SocketTest, ConnectFailure) {
  run([&]() -> Task<> {
    ScopedBoundPort ph;

    auto serverAddr = ph.getAddress();
    EXPECT_THROW(
        co_await Socket::connect(&evb, serverAddr, 0ms), AsyncSocketException);
  });
}

TEST_F(ServerSocketTest, ConnectSuccess) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    EXPECT_EQ(srv.getAddress(), cs.getPeerAddress());
  });
}

TEST_F(ServerSocketTest, ConnectCancelled) {
  run([&]() -> Task<> {
    co_await folly::coro::collectAll(
        // token would be cancelled while waiting on connect
        [&]() -> Task<> {
          EXPECT_THROW(
              co_await co_withCancellation(cancelSource.getToken(), connect()),
              OperationCancelled);
        }(),
        requestCancellation());
    // token was cancelled before read was called
    EXPECT_THROW(
        co_await co_withCancellation(
            cancelSource.getToken(),
            Socket::connect(&evb, srv.getAddress(), 0ms)),
        OperationCancelled);
  });
}

TEST_F(ServerSocketTest, SimpleRead) {
  run([&]() -> Task<> {
    constexpr auto kBufSize = 65536;
    auto cs = co_await connect();
    // produces blocking socket
    auto ss = srv.accept(-1);

    std::array<uint8_t, kBufSize> sndBuf;
    std::memset(sndBuf.data(), 'a', sndBuf.size());
    ss->write(sndBuf.data(), sndBuf.size());

    // read using coroutines
    std::array<uint8_t, kBufSize> rcvBuf;
    auto reader = [&rcvBuf, &cs]() -> Task<Unit> {
      int totalBytes{0};
      while (totalBytes < kBufSize) {
        auto bytesRead = co_await cs.read(
            MutableByteRange(
                rcvBuf.data() + totalBytes,
                (rcvBuf.data() + rcvBuf.size() - totalBytes)),
            0ms);
        totalBytes += bytesRead;
      }
      co_return unit;
    };

    co_await reader();
    EXPECT_EQ(0, memcmp(sndBuf.data(), rcvBuf.data(), rcvBuf.size()));
  });
}

TEST_F(ServerSocketTest, SimpleIOBufRead) {
  run([&]() -> Task<> {
    // Exactly fills a buffer mid-loop and triggers deferredReadEOF handling
    constexpr auto kBufSize = 55 * 1184;

    auto cs = co_await connect();
    // produces blocking socket
    auto ss = srv.accept(-1);

    std::array<uint8_t, kBufSize> sndBuf;
    std::memset(sndBuf.data(), 'a', sndBuf.size());
    ss->write(sndBuf.data(), sndBuf.size());
    ss->close();

    // read using coroutines
    IOBufQueue rcvBuf(IOBufQueue::cacheChainLength());
    int totalBytes{0};
    while (totalBytes < kBufSize) {
      auto bytesRead = co_await cs.read(rcvBuf, 1000, 1000, 0ms);
      totalBytes += bytesRead;
    }
    auto bytesRead = co_await cs.read(rcvBuf, 1000, 1000, 50ms);
    EXPECT_EQ(bytesRead, 0); // closed

    auto data = rcvBuf.move();
    data->coalesce();
    EXPECT_EQ(0, memcmp(sndBuf.data(), data->data(), data->length()));
  });
}

TEST_F(ServerSocketTest, ReadCancelled) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    auto reader = [&cs]() -> Task<Unit> {
      std::array<uint8_t, 1024> rcvBuf;
      EXPECT_THROW(
          co_await cs.read(
              MutableByteRange(rcvBuf.data(), (rcvBuf.data() + rcvBuf.size())),
              0ms),
          OperationCancelled);
      co_return unit;
    };

    co_await co_withCancellation(
        cancelSource.getToken(),
        folly::coro::collectAll(requestCancellation(), reader()));
    // token was cancelled before read was called
    co_await co_withCancellation(cancelSource.getToken(), reader());
  });
}

TEST_F(ServerSocketTest, ReadTimeout) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    std::array<uint8_t, 1024> rcvBuf;
    EXPECT_THROW(
        co_await cs.read(
            MutableByteRange(rcvBuf.data(), (rcvBuf.data() + rcvBuf.size())),
            50ms),
        AsyncSocketException);
  });
}

TEST_F(ServerSocketTest, ReadError) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    // produces blocking socket
    auto ss = srv.accept(-1);
    ss->closeWithReset();

    std::array<uint8_t, 1024> rcvBuf;
    EXPECT_THROW(
        co_await cs.read(
            MutableByteRange(rcvBuf.data(), (rcvBuf.data() + rcvBuf.size())),
            50ms),
        AsyncSocketException);
  });
}

TEST_F(ServerSocketTest, SimpleWrite) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    // produces blocking socket
    auto ss = srv.accept(-1);

    constexpr auto kBufSize = 65536;
    std::array<uint8_t, kBufSize> sndBuf;
    std::memset(sndBuf.data(), 'a', sndBuf.size());

    // write use co-routine
    co_await cs.write(ByteRange(sndBuf.data(), sndBuf.data() + sndBuf.size()));
    // read on server side
    std::array<uint8_t, kBufSize> rcvBuf;
    ss->readAll(rcvBuf.data(), rcvBuf.size());

    EXPECT_EQ(0, memcmp(sndBuf.data(), rcvBuf.data(), rcvBuf.size()));
  });
}

TEST_F(ServerSocketTest, SimpleWritev) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    // produces blocking socket
    auto ss = srv.accept(-1);

    IOBufQueue sndBuf;
    constexpr auto kBufSize = 65536;
    std::array<uint8_t, kBufSize> bufA;
    std::memset(bufA.data(), 'a', bufA.size());
    std::array<uint8_t, kBufSize> bufB;
    std::memset(bufB.data(), 'b', bufB.size());
    sndBuf.append(bufA.data(), bufA.size());
    sndBuf.append(bufB.data(), bufB.size());

    // write use co-routine
    co_await cs.write(sndBuf);

    // read on server side
    std::array<uint8_t, kBufSize> rcvBufA;
    ss->readAll(rcvBufA.data(), rcvBufA.size());
    EXPECT_EQ(0, memcmp(bufA.data(), rcvBufA.data(), rcvBufA.size()));
    std::array<uint8_t, kBufSize> rcvBufB;
    ss->readAll(rcvBufB.data(), rcvBufB.size());
    EXPECT_EQ(0, memcmp(bufB.data(), rcvBufB.data(), rcvBufB.size()));
  });
}

TEST_F(ServerSocketTest, WriteCancelled) {
  run([&]() -> Task<> {
    auto cs = co_await connect();
    // reduce the send buffer size so the write wouldn't complete immediately
    EXPECT_EQ(cs.getAsyncSocket()->setSendBufSize(4096), 0);
    // produces blocking socket
    auto ss = srv.accept(-1);
    constexpr auto kBufSize = 65536;
    std::array<uint8_t, kBufSize> sndBuf;
    std::memset(sndBuf.data(), 'a', sndBuf.size());

    // write use co-routine
    auto writter = [&]() -> Task<> {
      EXPECT_THROW(
          co_await co_withCancellation(
              cancelSource.getToken(),
              cs.write(
                  ByteRange(sndBuf.data(), sndBuf.data() + sndBuf.size()))),
          OperationCancelled);
    };

    co_await folly::coro::collectAll(requestCancellation(), writter());
    co_await co_withCancellation(cancelSource.getToken(), writter());
  });
}

TEST_F(SocketTest, SimpleAccept) {
  run([&]() -> Task<> {
    ServerSocket css(AsyncServerSocket::newSocket(&evb), std::nullopt, 16);
    auto serverAddr = css.getAsyncServerSocket()->getAddress();

    co_await folly::coro::collectAll(
        css.accept(), Socket::connect(&evb, serverAddr, 0ms));
  });
}

TEST_F(SocketTest, AcceptCancelled) {
  run([&]() -> Task<> {
    co_await folly::coro::collectAll(requestCancellation(), [&]() -> Task<> {
      ServerSocket css(AsyncServerSocket::newSocket(&evb), std::nullopt, 16);
      EXPECT_THROW(
          co_await co_withCancellation(cancelSource.getToken(), css.accept()),
          OperationCancelled);
    }());
  });
}

TEST_F(SocketTest, AsyncClientAndServer) {
  run([&]() -> Task<> {
    constexpr int kSize = 128;
    ServerSocket css(AsyncServerSocket::newSocket(&evb), std::nullopt, 16);
    auto serverAddr = css.getAsyncServerSocket()->getAddress();
    auto cs = co_await Socket::connect(&evb, serverAddr, 0ms);

    co_await folly::coro::collectAll(
        [&css]() -> Task<> {
          auto sock = co_await css.accept();
          std::array<uint8_t, kSize> buf;
          memset(buf.data(), 'a', kSize);
          co_await sock->write(ByteRange(buf.begin(), buf.end()));
          css.close();
        }(),

        [&cs]() -> Task<> {
          std::array<uint8_t, kSize> buf;
          // For fun, shutdown the write half -- we don't need it
          cs.shutdownWrite();
          auto len =
              co_await cs.read(MutableByteRange(buf.begin(), buf.end()), 0ms);
          cs.close();
          EXPECT_TRUE(len == buf.size());
        }());
  });
}

#endif // FOLLY_HAS_COROUTINES
