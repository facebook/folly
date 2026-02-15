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

#include <folly/io/async/test/ZeroCopy.h>

#include <folly/portability/GTest.h>

using namespace folly;

static auto constexpr kMaxLoops = 20;
static auto constexpr kBufferSize = 4096;
static auto constexpr kBufferSizeLarge = kBufferSize * 1024;

TEST(ZeroCopyTest, zeroCopyInProgress) {
  ZeroCopyTest test(1, kMaxLoops, true, kBufferSize);
  CHECK(test.run());
}

TEST(ZeroCopyTest, zeroCopyInProgressLargeClientClose) {
  ZeroCopyTest test(1, 1, true, kBufferSize);
  test.setSendBufSize(kBufferSizeLarge);
  test.setCloseAfterSend(true);
  CHECK(test.run());
}

TEST(ZeroCopyTest, zeroCopyInProgressLargeServerClose) {
  ZeroCopyTest test(1, kMaxLoops, true, kBufferSize);
  test.setCloseAfterAccept(true);
  CHECK(test.run());
}

namespace {

class ZeroCopyEnableThresholdCallback
    : public folly::AsyncSocket::SendMsgParamsCallback {
 public:
  int getFlagsImpl(
      folly::WriteFlags flags, int defaultFlags) noexcept override {
    lastWriteFlags_ = flags;
    return defaultFlags;
  }

  folly::WriteFlags getLastWriteFlags() const { return lastWriteFlags_; }

 private:
  folly::WriteFlags lastWriteFlags_{folly::WriteFlags::NONE};
};

class ZeroCopyEnableThresholdServer {
 public:
  explicit ZeroCopyEnableThresholdServer(folly::EventBase* evb)
      : listenSock_(new folly::AsyncServerSocket(evb)) {
    listenSock_->bind(0);
    listenSock_->listen(10);
    listenSock_->startAccepting();
  }

  folly::SocketAddress getAddress() const { return listenSock_->getAddress(); }

 private:
  folly::AsyncServerSocket::UniquePtr listenSock_;
};

} // namespace

TEST(ZeroCopyTest, zeroCopyEnableThresholdBelowThreshold) {
  EventBase evb;
  ZeroCopyEnableThresholdServer server(&evb);

  auto socket = AsyncSocket::newSocket(&evb);
  socket->connect(nullptr, server.getAddress());

  socket->setZeroCopy(true);
  socket->setZeroCopyEnableThreshold(1024);

  ZeroCopyEnableThresholdCallback callback;
  socket->setSendMsgParamCB(&callback);

  auto smallBuf = IOBuf::copyBuffer(std::string(512, 'a'));
  socket->writeChain(nullptr, std::move(smallBuf), WriteFlags::NONE);
  evb.loopOnce();

  EXPECT_FALSE(
      isSet(callback.getLastWriteFlags(), WriteFlags::WRITE_MSG_ZEROCOPY));
}

TEST(ZeroCopyTest, zeroCopyEnableThresholdAboveThreshold) {
  EventBase evb;
  ZeroCopyEnableThresholdServer server(&evb);

  auto socket = AsyncSocket::newSocket(&evb);
  socket->connect(nullptr, server.getAddress());

  socket->setZeroCopy(true);
  socket->setZeroCopyEnableThreshold(1024);

  ZeroCopyEnableThresholdCallback callback;
  socket->setSendMsgParamCB(&callback);

  auto largeBuf = IOBuf::copyBuffer(std::string(2048, 'a'));
  socket->writeChain(nullptr, std::move(largeBuf), WriteFlags::NONE);
  evb.loopOnce();

  EXPECT_TRUE(
      isSet(callback.getLastWriteFlags(), WriteFlags::WRITE_MSG_ZEROCOPY));
}

TEST(ZeroCopyTest, zeroCopyEnableThresholdDisabled) {
  EventBase evb;
  ZeroCopyEnableThresholdServer server(&evb);

  auto socket = AsyncSocket::newSocket(&evb);
  socket->connect(nullptr, server.getAddress());

  socket->setZeroCopy(true);

  ZeroCopyEnableThresholdCallback callback;
  socket->setSendMsgParamCB(&callback);

  auto largeBuf = IOBuf::copyBuffer(std::string(2048, 'a'));
  socket->writeChain(nullptr, std::move(largeBuf), WriteFlags::NONE);
  evb.loopOnce();

  EXPECT_FALSE(
      isSet(callback.getLastWriteFlags(), WriteFlags::WRITE_MSG_ZEROCOPY));
}

TEST(ZeroCopyTest, zeroCopyEnableThresholdExplicitFlagNotOverridden) {
  EventBase evb;
  ZeroCopyEnableThresholdServer server(&evb);

  auto socket = AsyncSocket::newSocket(&evb);
  socket->connect(nullptr, server.getAddress());

  socket->setZeroCopy(true);
  socket->setZeroCopyEnableThreshold(1024);

  ZeroCopyEnableThresholdCallback callback;
  socket->setSendMsgParamCB(&callback);

  auto smallBuf = IOBuf::copyBuffer(std::string(512, 'a'));
  socket->writeChain(
      nullptr, std::move(smallBuf), WriteFlags::WRITE_MSG_ZEROCOPY);
  evb.loopOnce();

  EXPECT_TRUE(
      isSet(callback.getLastWriteFlags(), WriteFlags::WRITE_MSG_ZEROCOPY));
}
