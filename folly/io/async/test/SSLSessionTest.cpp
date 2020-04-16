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

class SimpleCallbackManager {
 public:
  static int sessionCallback(SSL* ssl, SSL_SESSION* session) {
    auto sessionPtr = folly::ssl::SSLSessionUniquePtr(session);
    auto socket = folly::AsyncSSLSocket::getFromSSL(ssl);
    auto sslSession =
        std::dynamic_pointer_cast<folly::ssl::detail::OpenSSLSession>(
            socket->getSSLSessionV2());
    sslSession->setActiveSession(std::move(sessionPtr));
    return 1;
  }
};

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
  auto clientCtx_ = clientCtx->getSSLCtx();

  // The following two actions (setting session cache mode and
  // setting the session callback) will eventually be done in
  // SSLContext instead
  SSL_CTX_set_session_cache_mode(clientCtx_, SSL_SESS_CACHE_CLIENT);
  SSL_CTX_sess_set_new_cb(clientCtx_, SimpleCallbackManager::sessionCallback);

  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadCertificate(kTestCert);
  serverCtx->loadPrivateKey(kTestKey);
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

  sslSession = clientPtr->getSSLSessionV2();
  ASSERT_NE(sslSession, nullptr);

  // The underlying SSL_SESSION is set in the session callback
  // that is attached to the SSL_CTX. The session is guaranteed to
  // be resumable here in TLS 1.2, but not in TLS 1.3
  auto opensslSession = std::dynamic_pointer_cast<OpenSSLSession>(sslSession);
  auto sessionPtr = opensslSession->getActiveSession();
  ASSERT_NE(sessionPtr.get(), nullptr);
}

} // namespace folly
