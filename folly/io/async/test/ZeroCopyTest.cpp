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

#include <cstring>
#include <memory>
#include <vector>

#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>

#ifdef FOLLY_HAVE_MSG_ERRQUEUE
#include <linux/errqueue.h>
#endif

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

#ifdef FOLLY_HAVE_MSG_ERRQUEUE
namespace {
// AsyncSocket's zero-copy completion plumbing is protected+virtual, so a
// subclass exposes it for deterministic, kernel-independent testing of the
// StopTLS fd-handoff state transfer (moveZeroCopyStateFrom).
class ZeroCopyStateSocket : public folly::AsyncSocket {
 public:
  using folly::AsyncSocket::addZeroCopyBuf;
  using folly::AsyncSocket::AsyncSocket;
  using folly::AsyncSocket::moveZeroCopyStateFrom;
  using folly::AsyncSocket::processZeroCopyMsg;
  using UniquePtr = std::
      unique_ptr<ZeroCopyStateSocket, folly::DelayedDestruction::Destructor>;
};

class CountingReleaseCb : public folly::AsyncWriter::ReleaseIOBufCallback {
 public:
  void releaseIOBuf(std::unique_ptr<folly::IOBuf> buf) noexcept override {
    (void)buf;
    ++count;
  }
  int count{0};
};

// Builds a synthetic MSG_ERRQUEUE zero-copy completion cmsg covering buffer ids
// [lo, hi], matching what the kernel posts (SO_EE_ORIGIN_ZEROCOPY). Returns a
// pointer into `storage`, which the caller must keep alive.
const cmsghdr* makeZeroCopyCompletion(
    std::vector<char>& storage, uint32_t lo, uint32_t hi) {
  storage.assign(CMSG_SPACE(sizeof(sock_extended_err)), 0);
  auto* cmsg = reinterpret_cast<cmsghdr*>(storage.data());
  cmsg->cmsg_level = SOL_IPV6;
  cmsg->cmsg_type = IPV6_RECVERR;
  cmsg->cmsg_len = CMSG_LEN(sizeof(sock_extended_err));
  sock_extended_err serr = {};
  serr.ee_errno = 0;
  serr.ee_origin = SO_EE_ORIGIN_ZEROCOPY;
  serr.ee_info = lo;
  serr.ee_data = hi;
  memcpy(CMSG_DATA(cmsg), &serr, sizeof(serr));
  return cmsg;
}
} // namespace

// Repro of the D105586778 crash: a StopTLS fd handoff that does NOT transfer
// the outstanding zero-copy completion maps leaves the new socket unable to
// reconcile a kernel completion for an in-flight send, aborting in
// AsyncSocket::releaseZeroCopyBuf().
TEST(ZeroCopyStateTransferTest, UnknownCompletionAbortsWithoutTransfer) {
  folly::EventBase evb;
  ZeroCopyStateSocket::UniquePtr sock(new ZeroCopyStateSocket(&evb));
  std::vector<char> storage;
  const auto* cmsg = makeZeroCopyCompletion(storage, /*lo=*/0, /*hi=*/0);
  // Match the specific CHECK in releaseZeroCopyBuf so the test asserts the
  // documented failure, not any unrelated crash.
  EXPECT_DEATH(sock->processZeroCopyMsg(*cmsg), "idZeroCopyBufPtrMap_");
}

// With the fix, moveZeroCopyStateFrom() hands the outstanding completion state
// to the new socket, which then reaps the same completion cleanly.
TEST(ZeroCopyStateTransferTest, TransferLetsNewSocketReapCompletion) {
  folly::EventBase evb;
  ZeroCopyStateSocket::UniquePtr src(new ZeroCopyStateSocket(&evb));
  ZeroCopyStateSocket::UniquePtr dst(new ZeroCopyStateSocket(&evb));

  // Register an outstanding zero-copy buffer (id 0) on the source, as if a send
  // were in flight at handoff time. An empty IOBuf keeps buffered-bytes
  // accounting at zero so the move is exercised without a real write.
  CountingReleaseCb cb;
  src->addZeroCopyBuf(std::make_unique<folly::IOBuf>(), &cb);

  dst->moveZeroCopyStateFrom(*src);

  std::vector<char> storage;
  const auto* cmsg = makeZeroCopyCompletion(storage, /*lo=*/0, /*hi=*/0);
  dst->processZeroCopyMsg(*cmsg); // must not abort
  EXPECT_EQ(cb.count, 1);
}

// The CHECK enforces the atomicity invariant: zero-copy state may only be moved
// from a socket whose fd has already been detached, so the fd and the state it
// belongs to move together. Moving from a socket that still owns its fd is a
// partial transfer and must abort.
TEST(ZeroCopyStateTransferTest, MoveFromSocketThatStillOwnsFdAborts) {
  folly::EventBase evb;
  int fds[2];
  ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  ZeroCopyStateSocket::UniquePtr src(
      new ZeroCopyStateSocket(&evb, folly::NetworkSocket::fromFd(fds[0])));
  ZeroCopyStateSocket::UniquePtr dst(
      new ZeroCopyStateSocket(&evb, folly::NetworkSocket::fromFd(fds[1])));

  // The precondition CHECK fires on `src` still owning its fd, before any
  // zero-copy state is touched — no outstanding buffer is needed to trip it.
  // Setup stays outside EXPECT_DEATH so the death assertion is scoped to the
  // one offending call and the parent closes the socketpair cleanly.
  EXPECT_DEATH(dst->moveZeroCopyStateFrom(*src), "still owns its fd");
}
#endif // FOLLY_HAVE_MSG_ERRQUEUE
