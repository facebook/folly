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

#include <folly/ssl/SSLSession.h>
#include <folly/io/async/test/AsyncSSLSocketTest.h>
#include <folly/net/NetOps.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/GTest.h>
#include <folly/portability/OpenSSL.h>
#include <folly/portability/Sockets.h>
#include <folly/ssl/detail/OpenSSLSession.h>

#include <memory>

using folly::ssl::SSLSession;
using folly::ssl::detail::OpenSSLSession;

namespace folly {

void getfds(NetworkSocket fds[2]) {
  if (netops::socketpair(PF_LOCAL, SOCK_STREAM, 0, fds) != 0) {
    FAIL() << "failed to create socketpair: " << errnoStr(errno);
  }
  for (int idx = 0; idx < 2; ++idx) {
    if (netops::set_socket_non_blocking(fds[idx]) != 0) {
      FAIL() << "failed to put socket " << idx
             << " in non-blocking mode: " << errnoStr(errno);
    }
  }
}

void getctx(
    std::shared_ptr<folly::SSLContext> clientCtx,
    std::shared_ptr<folly::SSLContext> serverCtx) {
  clientCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  clientCtx->loadTrustedCertificates(kTestCA);
  clientCtx->enableTLS13();

  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadCertificate(kTestCert);
  serverCtx->loadPrivateKey(kTestKey);
  serverCtx->enableTLS13();
}

class SSLSessionTest : public testing::Test {
 public:
  void SetUp() override {
    clientCtx.reset(new folly::SSLContext());
    dfServerCtx.reset(new folly::SSLContext());
    hskServerCtx.reset(new folly::SSLContext());
    serverName = "xyz.newdev.facebook.com";
    getctx(clientCtx, dfServerCtx);
  }

  void TearDown() override {}

  folly::EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx;
  std::shared_ptr<SSLContext> dfServerCtx;
  // Use the same SSLContext to continue the handshake after
  // tlsext_hostname match.
  std::shared_ptr<SSLContext> hskServerCtx;
  std::string serverName;
};

TEST_F(SSLSessionTest, BasicTest) {
  std::shared_ptr<SSLSession> sslSession;

  // Full handshake
  {
    NetworkSocket fds[2];
    getfds(fds);

    AsyncSSLSocket::UniquePtr clientSock(
        new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
    auto clientPtr = clientSock.get();
    sslSession = clientPtr->getSSLSessionV2();
    ASSERT_NE(sslSession, nullptr);
    {
      auto opensslSession =
          std::dynamic_pointer_cast<OpenSSLSession>(sslSession);
      auto sessionPtr = opensslSession->getActiveSession();
      ASSERT_EQ(sessionPtr.get(), nullptr);
    }

    AsyncSSLSocket::UniquePtr serverSock(
        new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
    SSLHandshakeClient client(std::move(clientSock), false, false);
    SSLHandshakeServerParseClientHello server(
        std::move(serverSock), false, false);

    eventBase.loop();
    ASSERT_TRUE(client.handshakeSuccess_);
    ASSERT_FALSE(clientPtr->getSSLSessionReused());
    {
      auto opensslSession =
          std::dynamic_pointer_cast<OpenSSLSession>(sslSession);
      auto sessionPtr = opensslSession->getActiveSession();
      ASSERT_NE(sessionPtr.get(), nullptr);
    }
  }

  // Session resumption
  {
    NetworkSocket fds[2];
    getfds(fds);
    AsyncSSLSocket::UniquePtr clientSock(
        new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
    auto clientPtr = clientSock.get();

    clientPtr->setSSLSessionV2(sslSession);

    AsyncSSLSocket::UniquePtr serverSock(
        new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
    SSLHandshakeClient client(std::move(clientSock), false, false);
    SSLHandshakeServerParseClientHello server(
        std::move(serverSock), false, false);

    eventBase.loop();
    ASSERT_TRUE(client.handshakeSuccess_);
    ASSERT_TRUE(clientPtr->getSSLSessionReused());
  }
}

/**
 * To be removed when getSSLSessionV2() and setSSLSessionV2()
 * replace getSSLSession() and setSSLSession(),
 * respectively.
 */
TEST_F(SSLSessionTest, BasicRegressionTest) {
  SSL_SESSION* sslSession;

  // Full handshake
  {
    NetworkSocket fds[2];
    getfds(fds);
    AsyncSSLSocket::UniquePtr clientSock(
        new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
    auto clientPtr = clientSock.get();
    AsyncSSLSocket::UniquePtr serverSock(
        new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
    SSLHandshakeClient client(std::move(clientSock), false, false);
    SSLHandshakeServerParseClientHello server(
        std::move(serverSock), false, false);

    eventBase.loop();
    ASSERT_TRUE(client.handshakeSuccess_);
    ASSERT_FALSE(clientPtr->getSSLSessionReused());

    sslSession = clientPtr->getSSLSession();
    ASSERT_NE(sslSession, nullptr);
  }

  // Session resumption
  {
    NetworkSocket fds[2];
    getfds(fds);
    AsyncSSLSocket::UniquePtr clientSock(
        new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
    auto clientPtr = clientSock.get();

    clientPtr->setSSLSession(sslSession);

    AsyncSSLSocket::UniquePtr serverSock(
        new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
    SSLHandshakeClient client(std::move(clientSock), false, false);
    SSLHandshakeServerParseClientHello server(
        std::move(serverSock), false, false);

    eventBase.loop();
    ASSERT_TRUE(client.handshakeSuccess_);
    ASSERT_TRUE(clientPtr->getSSLSessionReused());
    SSL_SESSION_free(sslSession);
  }
}

TEST_F(SSLSessionTest, NullSessionResumptionTest) {
  // Set null session, should result in full handshake
  {
    NetworkSocket fds[2];
    getfds(fds);
    AsyncSSLSocket::UniquePtr clientSock(
        new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
    auto clientPtr = clientSock.get();

    clientPtr->setSSLSessionV2(nullptr);

    AsyncSSLSocket::UniquePtr serverSock(
        new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
    SSLHandshakeClient client(std::move(clientSock), false, false);
    SSLHandshakeServerParseClientHello server(
        std::move(serverSock), false, false);

    eventBase.loop();
    ASSERT_TRUE(client.handshakeSuccess_);
    ASSERT_FALSE(clientPtr->getSSLSessionReused());
  }
}

} // namespace folly
