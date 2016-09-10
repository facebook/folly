/*
 * Copyright 2016 Facebook, Inc.
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
#include <folly/io/async/test/AsyncSSLSocketTest.h>

#include <pthread.h>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/portability/GTest.h>

using std::string;
using std::vector;
using std::min;
using std::cerr;
using std::endl;
using std::list;

namespace folly {

class AttachDetachClient : public AsyncSocket::ConnectCallback,
                           public AsyncTransportWrapper::WriteCallback,
                           public AsyncTransportWrapper::ReadCallback {
 private:
  EventBase *eventBase_;
  std::shared_ptr<AsyncSSLSocket> sslSocket_;
  std::shared_ptr<SSLContext> ctx_;
  folly::SocketAddress address_;
  char buf_[128];
  char readbuf_[128];
  uint32_t bytesRead_;
 public:
  AttachDetachClient(EventBase *eventBase, const folly::SocketAddress& address)
      : eventBase_(eventBase), address_(address), bytesRead_(0) {
    ctx_.reset(new SSLContext());
    ctx_->setOptions(SSL_OP_NO_TICKET);
    ctx_->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  }

  void connect() {
    sslSocket_ = AsyncSSLSocket::newSocket(ctx_, eventBase_);
    sslSocket_->connect(this, address_);
  }

  void connectSuccess() noexcept override {
    cerr << "client SSL socket connected" << endl;

    for (int i = 0; i < 1000; ++i) {
      sslSocket_->detachSSLContext();
      sslSocket_->attachSSLContext(ctx_);
    }

    EXPECT_EQ(ctx_->getSSLCtx()->references, 2);

    sslSocket_->write(this, buf_, sizeof(buf_));
    sslSocket_->setReadCB(this);
    memset(readbuf_, 'b', sizeof(readbuf_));
    bytesRead_ = 0;
  }

  void connectErr(const AsyncSocketException& ex) noexcept override
  {
    cerr << "AttachDetachClient::connectError: " << ex.what() << endl;
    sslSocket_.reset();
  }

  void writeSuccess() noexcept override {
    cerr << "client write success" << endl;
  }

  void writeErr(size_t /* bytesWritten */,
                const AsyncSocketException& ex) noexcept override {
    cerr << "client writeError: " << ex.what() << endl;
  }

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override {
    *bufReturn = readbuf_ + bytesRead_;
    *lenReturn = sizeof(readbuf_) - bytesRead_;
  }
  void readEOF() noexcept override {
    cerr << "client readEOF" << endl;
  }

  void readErr(const AsyncSocketException& ex) noexcept override {
    cerr << "client readError: " << ex.what() << endl;
  }

  void readDataAvailable(size_t len) noexcept override {
    cerr << "client read data: " << len << endl;
    bytesRead_ += len;
    if (len == sizeof(buf_)) {
      EXPECT_EQ(memcmp(buf_, readbuf_, bytesRead_), 0);
      sslSocket_->closeNow();
    }
  }
};

/**
 * Test passing contexts between threads
 */
TEST(AsyncSSLSocketTest2, AttachDetachSSLContext) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallbackDelay acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  EventBase eventBase;
  EventBaseAborter eba(&eventBase, 3000);
  std::shared_ptr<AttachDetachClient> client(
    new AttachDetachClient(&eventBase, server.getAddress()));

  client->connect();
  eventBase.loop();
}

}  // folly

int main(int argc, char *argv[]) {
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif
  folly::SSLContext::setSSLLockTypes({
      {CRYPTO_LOCK_EVP_PKEY, folly::SSLContext::LOCK_NONE},
      {CRYPTO_LOCK_SSL_SESSION, folly::SSLContext::LOCK_SPINLOCK},
      {CRYPTO_LOCK_SSL_CTX, folly::SSLContext::LOCK_NONE}});
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
