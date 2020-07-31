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

#include <folly/io/Cursor.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <string>
#include <vector>

using namespace testing;

namespace folly {

class MockAsyncSSLSocket : public AsyncSSLSocket {
 public:
  static std::shared_ptr<MockAsyncSSLSocket> newSocket(
      const std::shared_ptr<SSLContext>& ctx,
      EventBase* evb) {
    auto sock = std::shared_ptr<MockAsyncSSLSocket>(
        new MockAsyncSSLSocket(ctx, evb), Destructor());
    sock->ssl_.reset(SSL_new(ctx->getSSLCtx()));
    SSL_set_fd(sock->ssl_.get(), -1);
    sock->setupSSLBio();
    return sock;
  }

  // Fake constructor sets the state to established without call to connect
  // or accept
  MockAsyncSSLSocket(const std::shared_ptr<SSLContext>& ctx, EventBase* evb)
      : AsyncSSLSocket(ctx, evb) {
    state_ = AsyncSocket::StateEnum::ESTABLISHED;
    sslState_ = AsyncSSLSocket::SSLStateEnum::STATE_ESTABLISHED;
  }

  // mock the calls to SSL_write to see the buffer length and contents
  MOCK_METHOD3(sslWriteImpl, int(SSL* ssl, const void* buf, int n));

  // mock the calls to SSL_get_error to insert errors
  MOCK_METHOD2(sslGetErrorImpl, int(const SSL* s, int ret_code));

  // mock the calls to sendSocketMessage to see the msg_flags
  MOCK_METHOD3(
      sendSocketMessage,
      AsyncSocket::WriteResult(
          NetworkSocket fd,
          struct msghdr* msg,
          int msg_flags));

  // mock the calls to getRawBytesWritten()
  MOCK_CONST_METHOD0(getRawBytesWritten, size_t());

  // public wrapper for protected interface
  WriteResult testPerformWrite(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten) {
    return performWrite(vec, count, flags, countWritten, partialWritten);
  }

  // public wrapper for protected member
  folly::Optional<size_t> getCurrBytesToFinalByte() const {
    return currBytesToFinalByte_;
  }
};

class AsyncSSLSocketWriteTest : public testing::Test {
 public:
  AsyncSSLSocketWriteTest()
      : sslContext_(new SSLContext()),
        sock_(MockAsyncSSLSocket::newSocket(sslContext_, &eventBase_)) {
    for (int i = 0; i < 500; i++) {
      memcpy(source_ + i * 26, "abcdefghijklmnopqrstuvwxyz", 26);
    }
  }

  // Make an iovec containing chunks of the reference text with requested sizes
  // for each chunk
  std::unique_ptr<iovec[]> makeVec(std::vector<uint32_t> sizes) {
    std::unique_ptr<iovec[]> vec(new iovec[sizes.size()]);
    int i = 0;
    int pos = 0;
    for (auto size : sizes) {
      vec[i].iov_base = (void*)(source_ + pos);
      vec[i++].iov_len = size;
      pos += size;
    }
    return vec;
  }

  // Verify that the given buf/pos matches the reference text
  void verifyVec(const void* buf, int n, int pos) {
    ASSERT_EQ(memcmp(source_ + pos, buf, n), 0);
  }

  // Update a vec on partial write
  void consumeVec(iovec* vec, uint32_t countWritten, uint32_t partialWritten) {
    vec[countWritten].iov_base =
        ((char*)vec[countWritten].iov_base) + partialWritten;
    vec[countWritten].iov_len -= partialWritten;
  }

  EventBase eventBase_;
  std::shared_ptr<SSLContext> sslContext_;
  std::shared_ptr<MockAsyncSSLSocket> sock_;
  char source_[26 * 500];
};

TEST_F(AsyncSSLSocketWriteTest, CompleteSSLWriteUpdatesAppBytesWritten) {
  int n = 1;
  auto vec = makeVec({1500});
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  // full write
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=](SSL* ssl, const void* buf, int m) {
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));

  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(sock_->getAppBytesWritten(), 1500);
}

TEST_F(AsyncSSLSocketWriteTest, NoSSLWriteUpdatesAppBytesWritten) {
  int n = 1;
  auto vec = makeVec({1500});
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  // want write
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=](SSL* ssl, const void* buf, int m) {
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(0))));
  EXPECT_CALL(*(sock_.get()), sslGetErrorImpl(_, _))
      .WillOnce(Return(SSL_ERROR_WANT_WRITE));

  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  // We got SSL_WANT_WRITE so should be 0
  EXPECT_EQ(sock_->getAppBytesWritten(), 0);
}

TEST_F(AsyncSSLSocketWriteTest, PartialSSLWriteUpdatesAppBytesWritten) {
  int n = 1;
  auto vec = makeVec({1500});
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  // partial write
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=](SSL* ssl, const void* buf, int m) {
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(500))));

  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(sock_->getAppBytesWritten(), 500);
}

// SSL_ERROR_WANT_WRITE occurs on first write
TEST_F(AsyncSSLSocketWriteTest, SslErrorWantWrite) {
  int n = 1;
  auto vec = makeVec({1500});
  int pos = 0;

  // first time we try to write, SSL_ERROR_WANT_WRITE will be returned
  //
  // this means no bytes were actually written to the socket,
  // but getRawBytesWritten will still  be incremented by the write size as
  // the bytes were appended to the BIO
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(0))));
  EXPECT_CALL(*(sock_.get()), sslGetErrorImpl(_, _))
      .WillOnce(Return(SSL_ERROR_WANT_WRITE));
  ON_CALL( // should not be called, unless implementation changes to use it
      *(sock_.get()),
      getRawBytesWritten())
      .WillByDefault(Return(1500));

  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 0);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 0);

  // second time we try to write, same buffer should be passed in
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1500);
}

// The entire vec fits in one packet
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing1) {
  int n = 3;
  auto vec = makeVec({3, 3, 3});
  int pos = 0;
  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 9))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL)) // no MSG_MORE
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(9))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 9);
}

// First packet is full, second two go in one packet
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing2) {
  int n = 3;
  auto vec = makeVec({1500, 3, 3});
  int pos = 0;
  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 6))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL)) // no MSG_MORE
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(6))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1506);
}

// Two exactly full packets (coalesce ends midway through second chunk)
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing3) {
  int n = 3;
  auto vec = makeVec({1000, 1000, 1000});
  int pos = 0;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .Times(2)
      .WillRepeatedly(Invoke([this, &pos](SSL*, const void* buf, int m) {
        verifyVec(buf, m, pos);
        pos += m;
        return m;
      }));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 3000);
}

// Partial write success midway through a coalesced vec
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing4) {
  int n = 5;
  auto vec = makeVec({300, 300, 300, 300, 300});
  int pos = 0;
  InSequence s1;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL)) // no MSG_MORE
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1000))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 3);
  EXPECT_EQ(partialWritten, 100);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1000);
  consumeVec(vec.get(), countWritten, partialWritten);

  InSequence s2;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(500))));
  sock_->testPerformWrite(
      vec.get() + countWritten,
      n - countWritten,
      WriteFlags::NONE,
      &countWritten,
      &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 2);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1500);
}

// coalesce ends exactly on a buffer boundary
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing5) {
  int n = 3;
  auto vec = makeVec({1000, 500, 500});
  int pos = 0;
  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(500))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 3);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 2000);
}

// partial write midway through first chunk
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescing6) {
  int n = 2;
  auto vec = makeVec({1000, 500});
  int pos = 0;

  InSequence s1;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL)) // no MSG_MORE
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(700))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::NONE, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 0);
  EXPECT_EQ(partialWritten, 700);
  EXPECT_EQ(sock_->getAppBytesWritten(), 700);
  consumeVec(vec.get(), countWritten, partialWritten);

  InSequence s2;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 800))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()), sendSocketMessage(_, _, MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(800))));
  sock_->testPerformWrite(
      vec.get() + countWritten,
      n - countWritten,
      WriteFlags::NONE,
      &countWritten,
      &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 2);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1500);
}

// Repeat coalescing2 with WriteFlags::EOR
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescingWithEoRTracking1) {
  int n = 3;
  auto vec = makeVec({1500, 3, 3});
  int pos = 0;
  EXPECT_FALSE(sock_->isEorTrackingEnabled());
  sock_->setEorTracking(true);
  EXPECT_TRUE(sock_->isEorTrackingEnabled());

  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        // the first 1500 does not have the EOR byte
        EXPECT_EQ(folly::none, sock_->getCurrBytesToFinalByte());
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 6))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(6))));

  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::EOR, &countWritten, &partialWritten);
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1506);
}

// coalescing with left over at the last chunk
// WriteFlags::EOR turned on
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescingWithEoRTracking2) {
  int n = 3;
  auto vec = makeVec({600, 600, 600});
  int pos = 0;
  sock_->setEorTracking(true);

  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        // the first 1500 does not have the EOR byte
        EXPECT_EQ(folly::none, sock_->getCurrBytesToFinalByte());
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 300))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(300))));

  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::EOR, &countWritten, &partialWritten);
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1800);
}

// WriteFlags::EOR set
// One buf in iovec
// Partial write at 1000-th byte
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescingWithEoRTracking3) {
  int n = 1;
  auto vec = makeVec({1600});
  int pos = 0;
  sock_->setEorTracking(true);

  InSequence s;
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1600))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        // partial write of 1000 bytes
        // currBytesToFinalByte should be 1600 at this point; expect full write
        EXPECT_EQ(1600, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1000))));
  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::EOR, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 0);
  EXPECT_EQ(partialWritten, 1000);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1000);
  consumeVec(vec.get(), countWritten, partialWritten);

  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 600))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(600))));
  sock_->testPerformWrite(
      vec.get() + countWritten,
      n - countWritten,
      WriteFlags::EOR,
      &countWritten,
      &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1600);
}

// WriteFlags::EOR set
// SSL_ERROR_WANT_WRITE occurs on first write
TEST_F(AsyncSSLSocketWriteTest, WriteCoalescingWithEoRTrackingErrorWantWrite) {
  int n = 1;
  auto vec = makeVec({1500});
  int pos = 0;
  sock_->setEorTracking(true);

  // first time we try to write, SSL_ERROR_WANT_WRITE will be returned
  //
  // this means no bytes were actually written to the socket,
  // but getRawBytesWritten will still  be incremented by the write size as
  // the bytes were appended to the BIO
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(0))));
  EXPECT_CALL(*(sock_.get()), sslGetErrorImpl(_, _))
      .WillOnce(Return(SSL_ERROR_WANT_WRITE));
  ON_CALL( // should not be called, unless implementation changes to use it
      *(sock_.get()),
      getRawBytesWritten())
      .WillByDefault(Return(1500));

  uint32_t countWritten = 0;
  uint32_t partialWritten = 0;
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::EOR, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, 0);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 0);

  // second time we try to write, no error
  // EOR should still be set
  EXPECT_CALL(*(sock_.get()), sslWriteImpl(_, _, 1500))
      .WillOnce(Invoke([=, &pos](SSL* ssl, const void* buf, int m) {
        EXPECT_EQ(m, sock_->getCurrBytesToFinalByte().value_or(0));
        verifyVec(buf, m, pos);
        BIO* b = SSL_get_wbio(ssl);
        auto result = AsyncSSLSocket::bioWrite(b, (const char*)buf, m);
        pos += result;
        return result;
      }));
  EXPECT_CALL(
      *(sock_.get()),
      sendSocketMessage(_, _, MSG_EOR | MSG_DONTWAIT | MSG_NOSIGNAL))
      .WillOnce(Return(ByMove(AsyncSocket::WriteResult(1500))));
  sock_->testPerformWrite(
      vec.get(), n, WriteFlags::EOR, &countWritten, &partialWritten);
  Mock::VerifyAndClearExpectations(sock_.get());
  EXPECT_EQ(countWritten, n);
  EXPECT_EQ(partialWritten, 0);
  EXPECT_EQ(sock_->getAppBytesWritten(), 1500);
}

} // namespace folly
