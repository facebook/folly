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

#include <folly/io/async/fdsock/AsyncFdSocket.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <folly/portability/GMock.h> // for matchers like HasSubstr
#include <folly/portability/GTest.h>

using namespace folly;

// `AsyncFdSocket` is just a stub on Windows because its CMSG macros are busted
#if !defined(_WIN32)

std::string getFdProcMagic(int fd) {
  char magic[PATH_MAX];
  PCHECK(
      -1 !=
      ::readlink(
          fmt::format("/proc/{}/fd/{}", getpid(), fd).c_str(),
          magic,
          PATH_MAX));
  return std::string(magic);
}

void checkFdsMatch(
    const folly::SocketFds::ToSend& sFds,
    const folly::SocketFds::Received& rFds) {
  EXPECT_EQ(sFds.size(), rFds.size());
  for (size_t i = 0; i < rFds.size(); ++i) {
    const auto& sfd = sFds[i]->fd();
    const auto& rfd = rFds[i].fd();

    // We should always receive with FD_CLOEXEC enabled
    int flags = ::fcntl(rfd, F_GETFD);
    PCHECK(-1 != flags);
    EXPECT_TRUE(flags & FD_CLOEXEC) << "Actual flags:" << flags;

    // Check that the two FDs appear identical in `/proc/PID/fd/FD`
    CHECK_EQ(getFdProcMagic(sfd), getFdProcMagic(rfd));
  }
}

folly::SocketFds::ToSend makeFdsToSend(size_t n) {
  folly::SocketFds::ToSend sendFds;
  while (sendFds.size() != n) {
    std::array<int, 2> rawSendFds;
    // Don't set FD_CLOEXEC here so it is extra-clear that the socket did it
    // (even though FD_CLOEXEC on this FD isn't coupled with its copy).
    PCHECK(0 == ::pipe(rawSendFds.data()));

    for (int fd : rawSendFds) {
      auto f = std::make_shared<folly::File>(fd, /*ownsFd*/ true);
      if (sendFds.size() < n) { // handle odd `n`
        sendFds.emplace_back(std::move(f));
      }
    }
  }
  CHECK_EQ(n, sendFds.size());
  return sendFds;
}

struct AsyncFdSocketTest : public testing::Test {
  AsyncFdSocketTest()
      : AsyncFdSocketTest{[]() {
          std::array<NetworkSocket, 2> fds;
          PCHECK(0 == netops::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
          for (int i = 0; i < 2; ++i) {
            PCHECK(0 == netops::set_socket_non_blocking(fds[i])) << i;
          }
          return fds;
        }()} {}

  explicit AsyncFdSocketTest(std::array<NetworkSocket, 2> fds)
      : sendSock_{&evb_, fds[0]}, recvSock_{&evb_, fds[1]} {
    recvSock_.setReadCB(&rcb_);
  }

  EventBase evb_;

  WriteCallback wcb_;
  AsyncFdSocket sendSock_;

  ReadCallback rcb_; // NB: `~AsyncSocket` calls `rcb.readEOF`
  AsyncFdSocket recvSock_;
};

TEST_F(AsyncFdSocketTest, FailNoData) {
  sendSock_.writeChainWithFds(&wcb_, IOBuf::create(0), makeFdsToSend(1));
  EXPECT_THAT(wcb_.exception.what(), testing::HasSubstr("least 1 data byte"));
}

TEST_F(AsyncFdSocketTest, FailTooManyFds) {
  char data = 'a'; // Need >= 1 data byte to send ancillary data.
  sendSock_.writeChainWithFds(
      &wcb_, IOBuf::wrapBuffer(&data, sizeof(data)), makeFdsToSend(254));
  EXPECT_EQ(EINVAL, wcb_.exception.getErrno());
}

struct AsyncFdSocketSimpleRoundtripTest
    : public AsyncFdSocketTest,
      public testing::WithParamInterface<int> {};

TEST_P(AsyncFdSocketSimpleRoundtripTest, WithNumFds) {
  int numFds = GetParam();
  char data = 'a'; // Need >= 1 data byte to send ancillary data.

  auto sendFds = makeFdsToSend(numFds);

  sendSock_.writeChainWithFds(
      &wcb_, IOBuf::wrapBuffer(&data, sizeof(data)), sendFds);
  evb_.loopOnce();

  rcb_.verifyData(&data, sizeof(data));
  rcb_.clearData();

  auto recvFds = recvSock_.popNextReceivedFds();
  if (0 == numFds) {
    EXPECT_FALSE(recvFds.has_value());
  } else {
    checkFdsMatch(sendFds, *recvFds);
  }
}

// Round-trip & verify various numbers of FDs with 1 byte of data.
INSTANTIATE_TEST_SUITE_P(
    VaryFdCount,
    AsyncFdSocketSimpleRoundtripTest,
    testing::Values(1, 0, 2, 253, 0, 3)); // 253 is the Linux max

struct WriteCountedAsyncFdSocket : public AsyncFdSocket {
  using AsyncFdSocket::AsyncFdSocket;
  AsyncSocket::WriteResult sendSocketMessage(
      const iovec* vec,
      size_t count,
      WriteFlags flags,
      WriteRequestTag writeTag) override {
    ++numWrites_;
    return AsyncSocket::sendSocketMessage(vec, count, flags, writeTag);
  }
  size_t numWrites_{0};
};

// This test exists because without `FdSendMsgParamsCallback::wroteBytes`,
// we would be sending the same FDs repeatedly, whenever the data to be
// written didn't fit in the `sendmsg` buffer and got sent in several
// batches.
TEST_F(AsyncFdSocketTest, MultiPartSend) {
  auto sendFds = makeFdsToSend(1);

  WriteCountedAsyncFdSocket sendSock{&evb_, sendSock_.detachNetworkSocket()};

  // Find the socket's "send" buffer size
  socklen_t sendBufSize;
  socklen_t sizeOfSendBufSize = sizeof(sendBufSize);
  PCHECK(
      0 ==
      netops::getsockopt(
          sendSock.getNetworkSocket(),
          SOL_SOCKET,
          SO_SNDBUF,
          &sendBufSize,
          &sizeOfSendBufSize));
  ASSERT_EQ(sizeof(sendBufSize), sizeOfSendBufSize);

  // Make the message too big for one `sendmsg`.
  int numSendParts = 3;
  std::string data(numSendParts * sendBufSize, 'x');
  sendSock.writeChainWithFds(
      &wcb_, IOBuf::wrapBuffer(data.data(), data.size()), sendFds);

  // FDs are sent with the first send & received by the first receive
  evb_.loopOnce();
  checkFdsMatch(sendFds, *recvSock_.popNextReceivedFds());
  EXPECT_EQ(1, sendSock.numWrites_);

  // Receive the rest of the data.
  while (rcb_.dataRead() < data.size()) {
    evb_.loopOnce();
  }
  rcb_.verifyData(data.data(), data.size());
  rcb_.clearData();
  EXPECT_EQ(numSendParts, sendSock.numWrites_);

  // There are no more data or FDs
  evb_.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_EQ(0, rcb_.dataRead()) << "Leftover reads";
  EXPECT_FALSE(recvSock_.popNextReceivedFds().has_value()) << "Extra FDs";
}

struct AsyncFdSocketSequenceRoundtripTest
    : public AsyncFdSocketTest,
      public testing::WithParamInterface<int> {};

TEST_P(AsyncFdSocketSequenceRoundtripTest, WithDataSize) {
  size_t dataSize = GetParam();

  // The default `ReadCallback` has special-snowflake buffer management
  // that's annoying for this test.  Secondarily, this exercises the
  // "ReadVec" path.
  ReadvCallback rcb(128, 3);
  // Avoid `readEOF` use-after-stack-scope in `~AsyncSocket`.
  SCOPE_EXIT { recvSock_.setReadCB(nullptr); };
  recvSock_.setReadCB(&rcb);

  std::queue<std::tuple<int, std::string, folly::SocketFds::ToSend>> sentQueue;

  // Enqueue several writes before attempting reads
  char msgFirstByte = 0; // The first byte of each data message is different.
  for (auto numFds : {0, 1, 0, 0, 2, 253, 253, 0, 5}) {
    // All but the first bytes are 255, while the first byte is a counter.
    std::string data(dataSize, '\377');
    CHECK_LT(msgFirstByte, 254); // Don't collide with 255, don't overflow
    data[0] = ++msgFirstByte;

    auto sendFds = makeFdsToSend(numFds);

    sendSock_.writeChainWithFds(
        &wcb_, IOBuf::wrapBuffer(data.data(), data.size()), sendFds);

    sentQueue.push({msgFirstByte, std::move(data), std::move(sendFds)});
  }

  // Read from the socket, and check that any batches of FDs arrive with the
  // first byte of the matching data.
  bool checkedFds{false};
  // The max expected steps is ~3k: 1234567 / (3 * 128)
  for (int i = 0; i < 10000 && !sentQueue.empty(); ++i) {
    evb_.loopOnce(EVLOOP_NONBLOCK);
    size_t dataRead = rcb.buf_->computeChainDataLength();
    if (!dataRead) {
      continue;
    }

    if (!checkedFds) {
      checkedFds = true;

      // Ensure that FDs arrive with the first byte of the associated data
      ASSERT_EQ(std::get<0>(sentQueue.front()), rcb.buf_->data()[0]);

      const auto& sendFds = std::get<2>(sentQueue.front());
      if (!sendFds.empty()) {
        auto recvFds = recvSock_.popNextReceivedFds();
        checkFdsMatch(sendFds, *recvFds);
      }
    }

    // Move on to the next data chunk
    if (dataRead >= std::get<1>(sentQueue.front()).size()) {
      ASSERT_TRUE(checkedFds);
      checkedFds = false;

      auto [firstByte, expectedData, _fds] = sentQueue.front();
      auto& buf = rcb.buf_;

      // Check that the entire data string is as expected.
      ASSERT_EQ(firstByte, buf->data()[0]);
      buf->coalesce();
      ASSERT_GE(buf->length(), expectedData.size());
      ASSERT_EQ(
          0, memcmp(expectedData.data(), buf->data(), expectedData.size()));
      buf->trimStart(expectedData.size());

      sentQueue.pop();
    }
  }
  EXPECT_TRUE(sentQueue.empty()) << "Stuck reading?";
  evb_.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_EQ(0, rcb.buf_->computeChainDataLength()) << "Leftover reads";
  EXPECT_FALSE(recvSock_.popNextReceivedFds().has_value()) << "Extra FDs";
}

// Vary the data size to (hopefully) get a variety of chunking behaviors.
INSTANTIATE_TEST_SUITE_P(
    VaryDataSize,
    AsyncFdSocketSequenceRoundtripTest,
    testing::Values(1, 12, 123, 1234, 12345, 123456, 1234567));

#endif // !Windows
