/*
 * Copyright 2015 Facebook, Inc.
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

#include <signal.h>
#include <pthread.h>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/SocketAddress.h>

#include <folly/io/async/test/BlockingSocket.h>

#include <gtest/gtest.h>
#include <iostream>
#include <list>
#include <set>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <folly/io/Cursor.h>

using std::string;
using std::vector;
using std::min;
using std::cerr;
using std::endl;
using std::list;

namespace folly {
uint32_t TestSSLAsyncCacheServer::asyncCallbacks_ = 0;
uint32_t TestSSLAsyncCacheServer::asyncLookups_ = 0;
uint32_t TestSSLAsyncCacheServer::lookupDelay_ = 0;

const char* testCert = "folly/io/async/test/certs/tests-cert.pem";
const char* testKey = "folly/io/async/test/certs/tests-key.pem";
const char* testCA = "folly/io/async/test/certs/ca-cert.pem";

constexpr size_t SSLClient::kMaxReadBufferSz;
constexpr size_t SSLClient::kMaxReadsPerEvent;

TestSSLServer::TestSSLServer(SSLServerAcceptCallbackBase *acb) :
ctx_(new folly::SSLContext),
    acb_(acb),
  socket_(folly::AsyncServerSocket::newSocket(&evb_)) {
  // Set up the SSL context
  ctx_->loadCertificate(testCert);
  ctx_->loadPrivateKey(testKey);
  ctx_->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  acb_->ctx_ = ctx_;
  acb_->base_ = &evb_;

  //set up the listening socket
  socket_->bind(0);
  socket_->getAddress(&address_);
  socket_->listen(100);
  socket_->addAcceptCallback(acb_, &evb_);
  socket_->startAccepting();

  int ret = pthread_create(&thread_, nullptr, Main, this);
  assert(ret == 0);
  (void)ret;

  std::cerr << "Accepting connections on " << address_ << std::endl;
}

void getfds(int fds[2]) {
  if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fds) != 0) {
    FAIL() << "failed to create socketpair: " << strerror(errno);
  }
  for (int idx = 0; idx < 2; ++idx) {
    int flags = fcntl(fds[idx], F_GETFL, 0);
    if (flags == -1) {
      FAIL() << "failed to get flags for socket " << idx << ": "
             << strerror(errno);
    }
    if (fcntl(fds[idx], F_SETFL, flags | O_NONBLOCK) != 0) {
      FAIL() << "failed to put socket " << idx << " in non-blocking mode: "
             << strerror(errno);
    }
  }
}

void getctx(
  std::shared_ptr<folly::SSLContext> clientCtx,
  std::shared_ptr<folly::SSLContext> serverCtx) {
  clientCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadCertificate(
      testCert);
  serverCtx->loadPrivateKey(
      testKey);
}

void sslsocketpair(
  EventBase* eventBase,
  AsyncSSLSocket::UniquePtr* clientSock,
  AsyncSSLSocket::UniquePtr* serverSock) {
  auto clientCtx = std::make_shared<folly::SSLContext>();
  auto serverCtx = std::make_shared<folly::SSLContext>();
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);
  clientSock->reset(new AsyncSSLSocket(
                      clientCtx, eventBase, fds[0], false));
  serverSock->reset(new AsyncSSLSocket(
                      serverCtx, eventBase, fds[1], true));

  // (*clientSock)->setSendTimeout(100);
  // (*serverSock)->setSendTimeout(100);
}

// client protocol filters
bool clientProtoFilterPickPony(unsigned char** client,
  unsigned int* client_len, const unsigned char*, unsigned int ) {
  //the protocol string in length prefixed byte string. the
  //length byte is not included in the length
  static unsigned char p[7] = {6,'p','o','n','i','e','s'};
  *client = p;
  *client_len = 7;
  return true;
}

bool clientProtoFilterPickNone(unsigned char**, unsigned int*,
  const unsigned char*, unsigned int) {
  return false;
}

/**
 * Test connecting to, writing to, reading from, and closing the
 * connection to the SSL server.
 */
TEST(AsyncSSLSocketTest, ConnectWriteReadClose) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallback acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL context.
  std::shared_ptr<SSLContext> sslContext(new SSLContext());
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  //sslContext->loadTrustedCertificates("./trusted-ca-certificate.pem");
  //sslContext->authenticate(true, false);

  // connect
  auto socket = std::make_shared<BlockingSocket>(server.getAddress(),
                                                 sslContext);
  socket->open();

  // write()
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  socket->write(buf, sizeof(buf));

  // read()
  uint8_t readbuf[128];
  uint32_t bytesRead = socket->readAll(readbuf, sizeof(readbuf));
  EXPECT_EQ(bytesRead, 128);
  EXPECT_EQ(memcmp(buf, readbuf, bytesRead), 0);

  // close()
  socket->close();

  cerr << "ConnectWriteReadClose test completed" << endl;
}

/**
 * Negative test for handshakeError().
 */
TEST(AsyncSSLSocketTest, HandshakeError) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  HandshakeErrorCallback acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL context.
  std::shared_ptr<SSLContext> sslContext(new SSLContext());
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  // connect
  auto socket = std::make_shared<BlockingSocket>(server.getAddress(),
                                                 sslContext);
  // read()
  bool ex = false;
  try {
    socket->open();

    uint8_t readbuf[128];
    uint32_t bytesRead = socket->readAll(readbuf, sizeof(readbuf));
    LOG(ERROR) << "readAll returned " << bytesRead << " instead of throwing";
  } catch (AsyncSocketException &e) {
    ex = true;
  }
  EXPECT_TRUE(ex);

  // close()
  socket->close();
  cerr << "HandshakeError test completed" << endl;
}

/**
 * Negative test for readError().
 */
TEST(AsyncSSLSocketTest, ReadError) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadErrorCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallback acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL context.
  std::shared_ptr<SSLContext> sslContext(new SSLContext());
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  // connect
  auto socket = std::make_shared<BlockingSocket>(server.getAddress(),
                                                 sslContext);
  socket->open();

  // write something to trigger ssl handshake
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  socket->write(buf, sizeof(buf));

  socket->close();
  cerr << "ReadError test completed" << endl;
}

/**
 * Negative test for writeError().
 */
TEST(AsyncSSLSocketTest, WriteError) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  WriteErrorCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallback acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL context.
  std::shared_ptr<SSLContext> sslContext(new SSLContext());
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  // connect
  auto socket = std::make_shared<BlockingSocket>(server.getAddress(),
                                                 sslContext);
  socket->open();

  // write something to trigger ssl handshake
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  socket->write(buf, sizeof(buf));

  socket->close();
  cerr << "WriteError test completed" << endl;
}

/**
 * Test a socket with TCP_NODELAY unset.
 */
TEST(AsyncSSLSocketTest, SocketWithDelay) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallbackDelay acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL context.
  std::shared_ptr<SSLContext> sslContext(new SSLContext());
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  // connect
  auto socket = std::make_shared<BlockingSocket>(server.getAddress(),
                                                 sslContext);
  socket->open();

  // write()
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  socket->write(buf, sizeof(buf));

  // read()
  uint8_t readbuf[128];
  uint32_t bytesRead = socket->readAll(readbuf, sizeof(readbuf));
  EXPECT_EQ(bytesRead, 128);
  EXPECT_EQ(memcmp(buf, readbuf, bytesRead), 0);

  // close()
  socket->close();

  cerr << "SocketWithDelay test completed" << endl;
}

TEST(AsyncSSLSocketTest, NpnTestOverlap) {
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> serverCtx(new SSLContext);;
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);

  clientCtx->setAdvertisedNextProtocols({"blub","baz"});
  serverCtx->setAdvertisedNextProtocols({"foo","bar","baz"});

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));
  NpnClient client(std::move(clientSock));
  NpnServer server(std::move(serverSock));

  eventBase.loop();

  EXPECT_TRUE(client.nextProtoLength != 0);
  EXPECT_EQ(client.nextProtoLength, server.nextProtoLength);
  EXPECT_EQ(memcmp(client.nextProto, server.nextProto,
                           server.nextProtoLength), 0);
  string selected((const char*)client.nextProto, client.nextProtoLength);
  EXPECT_EQ(selected.compare("baz"), 0);
}

TEST(AsyncSSLSocketTest, NpnTestUnset) {
  // Identical to above test, except that we want unset NPN before
  // looping.
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> serverCtx(new SSLContext);;
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);

  clientCtx->setAdvertisedNextProtocols({"blub","baz"});
  serverCtx->setAdvertisedNextProtocols({"foo","bar","baz"});

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));

  // unsetting NPN for any of [client, server] is enought to make NPN not
  // work
  clientCtx->unsetNextProtocols();

  NpnClient client(std::move(clientSock));
  NpnServer server(std::move(serverSock));

  eventBase.loop();

  EXPECT_TRUE(client.nextProtoLength == 0);
  EXPECT_TRUE(server.nextProtoLength == 0);
  EXPECT_TRUE(client.nextProto == nullptr);
  EXPECT_TRUE(server.nextProto == nullptr);
}

TEST(AsyncSSLSocketTest, NpnTestNoOverlap) {
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> serverCtx(new SSLContext);;
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);

  clientCtx->setAdvertisedNextProtocols({"blub"});
  serverCtx->setAdvertisedNextProtocols({"foo","bar","baz"});

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));
  NpnClient client(std::move(clientSock));
  NpnServer server(std::move(serverSock));

  eventBase.loop();

  EXPECT_TRUE(client.nextProtoLength != 0);
  EXPECT_EQ(client.nextProtoLength, server.nextProtoLength);
  EXPECT_EQ(memcmp(client.nextProto, server.nextProto,
                           server.nextProtoLength), 0);
  string selected((const char*)client.nextProto, client.nextProtoLength);
  EXPECT_EQ(selected.compare("blub"), 0);
}

TEST(AsyncSSLSocketTest, NpnTestClientProtoFilterHit) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);

  clientCtx->setAdvertisedNextProtocols({"blub"});
  clientCtx->setClientProtocolFilterCallback(clientProtoFilterPickPony);
  serverCtx->setAdvertisedNextProtocols({"foo","bar","baz"});

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));
  NpnClient client(std::move(clientSock));
  NpnServer server(std::move(serverSock));

  eventBase.loop();

  EXPECT_TRUE(client.nextProtoLength != 0);
  EXPECT_EQ(client.nextProtoLength, server.nextProtoLength);
  EXPECT_EQ(memcmp(client.nextProto, server.nextProto,
                           server.nextProtoLength), 0);
  string selected((const char*)client.nextProto, client.nextProtoLength);
  EXPECT_EQ(selected.compare("ponies"), 0);
}

TEST(AsyncSSLSocketTest, NpnTestClientProtoFilterMiss) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);

  clientCtx->setAdvertisedNextProtocols({"blub"});
  clientCtx->setClientProtocolFilterCallback(clientProtoFilterPickNone);
  serverCtx->setAdvertisedNextProtocols({"foo","bar","baz"});

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));
  NpnClient client(std::move(clientSock));
  NpnServer server(std::move(serverSock));

  eventBase.loop();

  EXPECT_TRUE(client.nextProtoLength != 0);
  EXPECT_EQ(client.nextProtoLength, server.nextProtoLength);
  EXPECT_EQ(memcmp(client.nextProto, server.nextProto,
                           server.nextProtoLength), 0);
  string selected((const char*)client.nextProto, client.nextProtoLength);
  EXPECT_EQ(selected.compare("blub"), 0);
}

TEST(AsyncSSLSocketTest, RandomizedNpnTest) {
  // Probability that this test will fail is 2^-64, which could be considered
  // as negligible.
  const int kTries = 64;

  std::set<string> selectedProtocols;
  for (int i = 0; i < kTries; ++i) {
    EventBase eventBase;
    std::shared_ptr<SSLContext> clientCtx = std::make_shared<SSLContext>();
    std::shared_ptr<SSLContext> serverCtx = std::make_shared<SSLContext>();
    int fds[2];
    getfds(fds);
    getctx(clientCtx, serverCtx);

    clientCtx->setAdvertisedNextProtocols({"foo", "bar", "baz"});
    serverCtx->setRandomizedAdvertisedNextProtocols({{1, {"foo"}},
        {1, {"bar"}}});


    AsyncSSLSocket::UniquePtr clientSock(
      new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
    AsyncSSLSocket::UniquePtr serverSock(
      new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));
    NpnClient client(std::move(clientSock));
    NpnServer server(std::move(serverSock));

    eventBase.loop();

    EXPECT_TRUE(client.nextProtoLength != 0);
    EXPECT_EQ(client.nextProtoLength, server.nextProtoLength);
    EXPECT_EQ(memcmp(client.nextProto, server.nextProto,
                             server.nextProtoLength), 0);
    string selected((const char*)client.nextProto, client.nextProtoLength);
    selectedProtocols.insert(selected);
  }
  EXPECT_EQ(selectedProtocols.size(), 2);
}


#ifndef OPENSSL_NO_TLSEXT
/**
 * 1. Client sends TLSEXT_HOSTNAME in client hello.
 * 2. Server found a match SSL_CTX and use this SSL_CTX to
 *    continue the SSL handshake.
 * 3. Server sends back TLSEXT_HOSTNAME in server hello.
 */
TEST(AsyncSSLSocketTest, SNITestMatch) {
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> dfServerCtx(new SSLContext);
  // Use the same SSLContext to continue the handshake after
  // tlsext_hostname match.
  std::shared_ptr<SSLContext> hskServerCtx(dfServerCtx);
  const std::string serverName("xyz.newdev.facebook.com");
  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
  SNIClient client(std::move(clientSock));
  SNIServer server(std::move(serverSock),
                   dfServerCtx,
                   hskServerCtx,
                   serverName);

  eventBase.loop();

  EXPECT_TRUE(client.serverNameMatch);
  EXPECT_TRUE(server.serverNameMatch);
}

/**
 * 1. Client sends TLSEXT_HOSTNAME in client hello.
 * 2. Server cannot find a matching SSL_CTX and continue to use
 *    the current SSL_CTX to do the handshake.
 * 3. Server does not send back TLSEXT_HOSTNAME in server hello.
 */
TEST(AsyncSSLSocketTest, SNITestNotMatch) {
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> dfServerCtx(new SSLContext);
  // Use the same SSLContext to continue the handshake after
  // tlsext_hostname match.
  std::shared_ptr<SSLContext> hskServerCtx(dfServerCtx);
  const std::string clientRequestingServerName("foo.com");
  const std::string serverExpectedServerName("xyz.newdev.facebook.com");

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx,
                        &eventBase,
                        fds[0],
                        clientRequestingServerName));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
  SNIClient client(std::move(clientSock));
  SNIServer server(std::move(serverSock),
                   dfServerCtx,
                   hskServerCtx,
                   serverExpectedServerName);

  eventBase.loop();

  EXPECT_TRUE(!client.serverNameMatch);
  EXPECT_TRUE(!server.serverNameMatch);
}
/**
 * 1. Client sends TLSEXT_HOSTNAME in client hello.
 * 2. We then change the serverName.
 * 3. We expect that we get 'false' as the result for serNameMatch.
 */

TEST(AsyncSSLSocketTest, SNITestChangeServerName) {
   EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> dfServerCtx(new SSLContext);
  // Use the same SSLContext to continue the handshake after
  // tlsext_hostname match.
  std::shared_ptr<SSLContext> hskServerCtx(dfServerCtx);
  const std::string serverName("xyz.newdev.facebook.com");
  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], serverName));
  //Change the server name
  std::string newName("new.com");
  clientSock->setServerName(newName);
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
  SNIClient client(std::move(clientSock));
  SNIServer server(std::move(serverSock),
                   dfServerCtx,
                   hskServerCtx,
                   serverName);

  eventBase.loop();

  EXPECT_TRUE(!client.serverNameMatch);
}

/**
 * 1. Client does not send TLSEXT_HOSTNAME in client hello.
 * 2. Server does not send back TLSEXT_HOSTNAME in server hello.
 */
TEST(AsyncSSLSocketTest, SNITestClientHelloNoHostname) {
  EventBase eventBase;
  std::shared_ptr<SSLContext> clientCtx(new SSLContext);
  std::shared_ptr<SSLContext> dfServerCtx(new SSLContext);
  // Use the same SSLContext to continue the handshake after
  // tlsext_hostname match.
  std::shared_ptr<SSLContext> hskServerCtx(dfServerCtx);
  const std::string serverExpectedServerName("xyz.newdev.facebook.com");

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));
  SNIClient client(std::move(clientSock));
  SNIServer server(std::move(serverSock),
                   dfServerCtx,
                   hskServerCtx,
                   serverExpectedServerName);

  eventBase.loop();

  EXPECT_TRUE(!client.serverNameMatch);
  EXPECT_TRUE(!server.serverNameMatch);
}

#endif
/**
 * Test SSL client socket
 */
TEST(AsyncSSLSocketTest, SSLClientTest) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallbackDelay acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             1));

  client->connect();
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  EXPECT_EQ(client->getMiss(), 1);
  EXPECT_EQ(client->getHit(), 0);

  cerr << "SSLClientTest test completed" << endl;
}


/**
 * Test SSL client socket session re-use
 */
TEST(AsyncSSLSocketTest, SSLClientTestReuse) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallbackDelay acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             10));

  client->connect();
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  EXPECT_EQ(client->getMiss(), 1);
  EXPECT_EQ(client->getHit(), 9);

  cerr << "SSLClientTestReuse test completed" << endl;
}

/**
 * Test SSL client socket timeout
 */
TEST(AsyncSSLSocketTest, SSLClientTimeoutTest) {
  // Start listening on a local port
  EmptyReadCallback readCallback;
  HandshakeCallback handshakeCallback(&readCallback,
                                      HandshakeCallback::EXPECT_ERROR);
  HandshakeTimeoutCallback acceptCallback(&handshakeCallback);
  TestSSLServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             1, 10));
  client->connect(true /* write before connect completes */);
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  usleep(100000);
  // This is checking that the connectError callback precedes any queued
  // writeError callbacks.  This matches AsyncSocket's behavior
  EXPECT_EQ(client->getWriteAfterConnectErrors(), 1);
  EXPECT_EQ(client->getErrors(), 1);
  EXPECT_EQ(client->getMiss(), 0);
  EXPECT_EQ(client->getHit(), 0);

  cerr << "SSLClientTimeoutTest test completed" << endl;
}


/**
 * Test SSL server async cache
 */
TEST(AsyncSSLSocketTest, SSLServerAsyncCacheTest) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAsyncCacheAcceptCallback acceptCallback(&handshakeCallback);
  TestSSLAsyncCacheServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             10, 500));

  client->connect();
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  EXPECT_EQ(server.getAsyncCallbacks(), 18);
  EXPECT_EQ(server.getAsyncLookups(), 9);
  EXPECT_EQ(client->getMiss(), 10);
  EXPECT_EQ(client->getHit(), 0);

  cerr << "SSLServerAsyncCacheTest test completed" << endl;
}


/**
 * Test SSL server accept timeout with cache path
 */
TEST(AsyncSSLSocketTest, SSLServerTimeoutTest) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  EmptyReadCallback clientReadCallback;
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAcceptCallback acceptCallback(&handshakeCallback, 50);
  TestSSLAsyncCacheServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  // only do a TCP connect
  std::shared_ptr<AsyncSocket> sock = AsyncSocket::newSocket(&eventBase);
  sock->connect(nullptr, server.getAddress());
  clientReadCallback.tcpSocket_ = sock;
  sock->setReadCB(&clientReadCallback);

  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  EXPECT_EQ(readCallback.state, STATE_WAITING);

  cerr << "SSLServerTimeoutTest test completed" << endl;
}

/**
 * Test SSL server accept timeout with cache path
 */
TEST(AsyncSSLSocketTest, SSLServerAsyncCacheTimeoutTest) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback);
  SSLServerAsyncCacheAcceptCallback acceptCallback(&handshakeCallback, 50);
  TestSSLAsyncCacheServer server(&acceptCallback);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             2));

  client->connect();
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  EXPECT_EQ(server.getAsyncCallbacks(), 1);
  EXPECT_EQ(server.getAsyncLookups(), 1);
  EXPECT_EQ(client->getErrors(), 1);
  EXPECT_EQ(client->getMiss(), 1);
  EXPECT_EQ(client->getHit(), 0);

  cerr << "SSLServerAsyncCacheTimeoutTest test completed" << endl;
}

/**
 * Test SSL server accept timeout with cache path
 */
TEST(AsyncSSLSocketTest, SSLServerCacheCloseTest) {
  // Start listening on a local port
  WriteCallbackBase writeCallback;
  ReadCallback readCallback(&writeCallback);
  HandshakeCallback handshakeCallback(&readCallback,
                                      HandshakeCallback::EXPECT_ERROR);
  SSLServerAsyncCacheAcceptCallback acceptCallback(&handshakeCallback);
  TestSSLAsyncCacheServer server(&acceptCallback, 500);

  // Set up SSL client
  EventBase eventBase;
  std::shared_ptr<SSLClient> client(new SSLClient(&eventBase, server.getAddress(),
                                             2, 100));

  client->connect();
  EventBaseAborter eba(&eventBase, 3000);
  eventBase.loop();

  server.getEventBase().runInEventBaseThread([&handshakeCallback]{
      handshakeCallback.closeSocket();});
  // give time for the cache lookup to come back and find it closed
  usleep(500000);

  EXPECT_EQ(server.getAsyncCallbacks(), 1);
  EXPECT_EQ(server.getAsyncLookups(), 1);
  EXPECT_EQ(client->getErrors(), 1);
  EXPECT_EQ(client->getMiss(), 1);
  EXPECT_EQ(client->getHit(), 0);

  cerr << "SSLServerCacheCloseTest test completed" << endl;
}

/**
 * Verify Client Ciphers obtained using SSL MSG Callback.
 */
TEST(AsyncSSLSocketTest, SSLParseClientHelloSuccess) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  serverCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  serverCtx->ciphers("RSA:!SHA:!NULL:!SHA256@STRENGTH");
  serverCtx->loadPrivateKey(testKey);
  serverCtx->loadCertificate(testCert);
  serverCtx->loadTrustedCertificates(testCA);
  serverCtx->loadClientCAList(testCA);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  clientCtx->ciphers("RC4-SHA:AES128-SHA:AES256-SHA:RC4-MD5");
  clientCtx->loadPrivateKey(testKey);
  clientCtx->loadCertificate(testCert);
  clientCtx->loadTrustedCertificates(testCA);

  int fds[2];
  getfds(fds);

  AsyncSSLSocket::UniquePtr clientSock(
      new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
      new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), true, true);
  SSLHandshakeServerParseClientHello server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_EQ(server.clientCiphers_,
            "RC4-SHA:AES128-SHA:AES256-SHA:RC4-MD5:00ff");
  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_TRUE(!client.handshakeError_);
  EXPECT_TRUE(server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_TRUE(!server.handshakeError_);
}

TEST(AsyncSSLSocketTest, SSLParseClientHelloOnePacket) {
  EventBase eventBase;
  auto ctx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);

  int bufLen = 42;
  uint8_t majorVersion = 18;
  uint8_t minorVersion = 25;

  // Create callback buf
  auto buf = IOBuf::create(bufLen);
  buf->append(bufLen);
  folly::io::RWPrivateCursor cursor(buf.get());
  cursor.write<uint8_t>(SSL3_MT_CLIENT_HELLO);
  cursor.write<uint16_t>(0);
  cursor.write<uint8_t>(38);
  cursor.write<uint8_t>(majorVersion);
  cursor.write<uint8_t>(minorVersion);
  cursor.skip(32);
  cursor.write<uint32_t>(0);

  SSL* ssl = ctx->createSSL();
  SCOPE_EXIT { SSL_free(ssl); };
  AsyncSSLSocket::UniquePtr sock(
      new AsyncSSLSocket(ctx, &eventBase, fds[0], true));
  sock->enableClientHelloParsing();

  // Test client hello parsing in one packet
  AsyncSSLSocket::clientHelloParsingCallback(
      0, 0, SSL3_RT_HANDSHAKE, buf->data(), buf->length(), ssl, sock.get());
  buf.reset();

  auto parsedClientHello = sock->getClientHelloInfo();
  EXPECT_TRUE(parsedClientHello != nullptr);
  EXPECT_EQ(parsedClientHello->clientHelloMajorVersion_, majorVersion);
  EXPECT_EQ(parsedClientHello->clientHelloMinorVersion_, minorVersion);
}

TEST(AsyncSSLSocketTest, SSLParseClientHelloTwoPackets) {
  EventBase eventBase;
  auto ctx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);

  int bufLen = 42;
  uint8_t majorVersion = 18;
  uint8_t minorVersion = 25;

  // Create callback buf
  auto buf = IOBuf::create(bufLen);
  buf->append(bufLen);
  folly::io::RWPrivateCursor cursor(buf.get());
  cursor.write<uint8_t>(SSL3_MT_CLIENT_HELLO);
  cursor.write<uint16_t>(0);
  cursor.write<uint8_t>(38);
  cursor.write<uint8_t>(majorVersion);
  cursor.write<uint8_t>(minorVersion);
  cursor.skip(32);
  cursor.write<uint32_t>(0);

  SSL* ssl = ctx->createSSL();
  SCOPE_EXIT { SSL_free(ssl); };
  AsyncSSLSocket::UniquePtr sock(
      new AsyncSSLSocket(ctx, &eventBase, fds[0], true));
  sock->enableClientHelloParsing();

  // Test parsing with two packets with first packet size < 3
  auto bufCopy = folly::IOBuf::copyBuffer(buf->data(), 2);
  AsyncSSLSocket::clientHelloParsingCallback(
      0, 0, SSL3_RT_HANDSHAKE, bufCopy->data(), bufCopy->length(),
      ssl, sock.get());
  bufCopy.reset();
  bufCopy = folly::IOBuf::copyBuffer(buf->data() + 2, buf->length() - 2);
  AsyncSSLSocket::clientHelloParsingCallback(
      0, 0, SSL3_RT_HANDSHAKE, bufCopy->data(), bufCopy->length(),
      ssl, sock.get());
  bufCopy.reset();

  auto parsedClientHello = sock->getClientHelloInfo();
  EXPECT_TRUE(parsedClientHello != nullptr);
  EXPECT_EQ(parsedClientHello->clientHelloMajorVersion_, majorVersion);
  EXPECT_EQ(parsedClientHello->clientHelloMinorVersion_, minorVersion);
}

TEST(AsyncSSLSocketTest, SSLParseClientHelloMultiplePackets) {
  EventBase eventBase;
  auto ctx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);

  int bufLen = 42;
  uint8_t majorVersion = 18;
  uint8_t minorVersion = 25;

  // Create callback buf
  auto buf = IOBuf::create(bufLen);
  buf->append(bufLen);
  folly::io::RWPrivateCursor cursor(buf.get());
  cursor.write<uint8_t>(SSL3_MT_CLIENT_HELLO);
  cursor.write<uint16_t>(0);
  cursor.write<uint8_t>(38);
  cursor.write<uint8_t>(majorVersion);
  cursor.write<uint8_t>(minorVersion);
  cursor.skip(32);
  cursor.write<uint32_t>(0);

  SSL* ssl = ctx->createSSL();
  SCOPE_EXIT { SSL_free(ssl); };
  AsyncSSLSocket::UniquePtr sock(
      new AsyncSSLSocket(ctx, &eventBase, fds[0], true));
  sock->enableClientHelloParsing();

  // Test parsing with multiple small packets
  for (uint64_t i = 0; i < buf->length(); i += 3) {
    auto bufCopy = folly::IOBuf::copyBuffer(
        buf->data() + i, std::min((uint64_t)3, buf->length() - i));
    AsyncSSLSocket::clientHelloParsingCallback(
        0, 0, SSL3_RT_HANDSHAKE, bufCopy->data(), bufCopy->length(),
        ssl, sock.get());
    bufCopy.reset();
  }

  auto parsedClientHello = sock->getClientHelloInfo();
  EXPECT_TRUE(parsedClientHello != nullptr);
  EXPECT_EQ(parsedClientHello->clientHelloMajorVersion_, majorVersion);
  EXPECT_EQ(parsedClientHello->clientHelloMinorVersion_, minorVersion);
}

/**
 * Verify sucessful behavior of SSL certificate validation.
 */
TEST(AsyncSSLSocketTest, SSLHandshakeValidationSuccess) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto dfServerCtx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  dfServerCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), true, true);
  clientCtx->loadTrustedCertificates(testCA);

  SSLHandshakeServer server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_TRUE(!client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(!server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_TRUE(!server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Verify that the client's verification callback is able to fail SSL
 * connection establishment.
 */
TEST(AsyncSSLSocketTest, SSLHandshakeValidationFailure) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto dfServerCtx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  dfServerCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), true, false);
  clientCtx->loadTrustedCertificates(testCA);

  SSLHandshakeServer server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(!client.handshakeSuccess_);
  EXPECT_TRUE(client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(!server.handshakeVerify_);
  EXPECT_TRUE(!server.handshakeSuccess_);
  EXPECT_TRUE(server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Verify that the options in SSLContext can be overridden in
 * sslConnect/Accept.i.e specifying that no validation should be performed
 * allows an otherwise-invalid certificate to be accepted and doesn't fire
 * the validation callback.
 */
TEST(AsyncSSLSocketTest, OverrideSSLCtxDisableVerify) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto dfServerCtx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  dfServerCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));

  SSLHandshakeClientNoVerify client(std::move(clientSock), false, false);
  clientCtx->loadTrustedCertificates(testCA);

  SSLHandshakeServerNoVerify server(std::move(serverSock), false, false);

  eventBase.loop();

  EXPECT_TRUE(!client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_TRUE(!client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(!server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_TRUE(!server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Verify that the options in SSLContext can be overridden in
 * sslConnect/Accept. Enable verification even if context says otherwise.
 * Test requireClientCert with client cert
 */
TEST(AsyncSSLSocketTest, OverrideSSLCtxEnableVerify) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  serverCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadPrivateKey(testKey);
  serverCtx->loadCertificate(testCert);
  serverCtx->loadTrustedCertificates(testCA);
  serverCtx->loadClientCAList(testCA);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  clientCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  clientCtx->loadPrivateKey(testKey);
  clientCtx->loadCertificate(testCert);
  clientCtx->loadTrustedCertificates(testCA);

  int fds[2];
  getfds(fds);

  AsyncSSLSocket::UniquePtr clientSock(
      new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
      new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));

  SSLHandshakeClientDoVerify client(std::move(clientSock), true, true);
  SSLHandshakeServerDoVerify server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_FALSE(client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_FALSE(server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Verify that the client's verification callback is able to override
 * the preverification failure and allow a successful connection.
 */
TEST(AsyncSSLSocketTest, SSLHandshakeValidationOverride) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto dfServerCtx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  dfServerCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), false, true);
  SSLHandshakeServer server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_TRUE(!client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(!server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_TRUE(!server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Verify that specifying that no validation should be performed allows an
 * otherwise-invalid certificate to be accepted and doesn't fire the validation
 * callback.
 */
TEST(AsyncSSLSocketTest, SSLHandshakeValidationSkip) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto dfServerCtx = std::make_shared<SSLContext>();

  int fds[2];
  getfds(fds);
  getctx(clientCtx, dfServerCtx);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  dfServerCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::NO_VERIFY);

  AsyncSSLSocket::UniquePtr clientSock(
    new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
    new AsyncSSLSocket(dfServerCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), false, false);
  SSLHandshakeServer server(std::move(serverSock), false, false);

  eventBase.loop();

  EXPECT_TRUE(!client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_TRUE(!client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(!server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_TRUE(!server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}

/**
 * Test requireClientCert with client cert
 */
TEST(AsyncSSLSocketTest, ClientCertHandshakeSuccess) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  serverCtx->setVerificationOption(
      SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadPrivateKey(testKey);
  serverCtx->loadCertificate(testCert);
  serverCtx->loadTrustedCertificates(testCA);
  serverCtx->loadClientCAList(testCA);

  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::VERIFY);
  clientCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  clientCtx->loadPrivateKey(testKey);
  clientCtx->loadCertificate(testCert);
  clientCtx->loadTrustedCertificates(testCA);

  int fds[2];
  getfds(fds);

  AsyncSSLSocket::UniquePtr clientSock(
      new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
      new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), true, true);
  SSLHandshakeServer server(std::move(serverSock), true, true);

  eventBase.loop();

  EXPECT_TRUE(client.handshakeVerify_);
  EXPECT_TRUE(client.handshakeSuccess_);
  EXPECT_FALSE(client.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_TRUE(server.handshakeVerify_);
  EXPECT_TRUE(server.handshakeSuccess_);
  EXPECT_FALSE(server.handshakeError_);
  EXPECT_LE(0, server.handshakeTime.count());
}


/**
 * Test requireClientCert with no client cert
 */
TEST(AsyncSSLSocketTest, NoClientCertHandshakeError) {
  EventBase eventBase;
  auto clientCtx = std::make_shared<SSLContext>();
  auto serverCtx = std::make_shared<SSLContext>();
  serverCtx->setVerificationOption(
      SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
  serverCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
  serverCtx->loadPrivateKey(testKey);
  serverCtx->loadCertificate(testCert);
  serverCtx->loadTrustedCertificates(testCA);
  serverCtx->loadClientCAList(testCA);
  clientCtx->setVerificationOption(SSLContext::SSLVerifyPeerEnum::NO_VERIFY);
  clientCtx->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  int fds[2];
  getfds(fds);

  AsyncSSLSocket::UniquePtr clientSock(
      new AsyncSSLSocket(clientCtx, &eventBase, fds[0], false));
  AsyncSSLSocket::UniquePtr serverSock(
      new AsyncSSLSocket(serverCtx, &eventBase, fds[1], true));

  SSLHandshakeClient client(std::move(clientSock), false, false);
  SSLHandshakeServer server(std::move(serverSock), false, false);

  eventBase.loop();

  EXPECT_FALSE(server.handshakeVerify_);
  EXPECT_FALSE(server.handshakeSuccess_);
  EXPECT_TRUE(server.handshakeError_);
  EXPECT_LE(0, client.handshakeTime.count());
  EXPECT_LE(0, server.handshakeTime.count());
}

TEST(AsyncSSLSocketTest, MinWriteSizeTest) {
  EventBase eb;

  // Set up SSL context.
  auto sslContext = std::make_shared<SSLContext>();
  sslContext->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  // create SSL socket
  AsyncSSLSocket::UniquePtr socket(new AsyncSSLSocket(sslContext, &eb));

  EXPECT_EQ(1500, socket->getMinWriteSize());

  socket->setMinWriteSize(0);
  EXPECT_EQ(0, socket->getMinWriteSize());
  socket->setMinWriteSize(50000);
  EXPECT_EQ(50000, socket->getMinWriteSize());
}

class ReadCallbackTerminator : public ReadCallback {
 public:
  ReadCallbackTerminator(EventBase* base, WriteCallbackBase *wcb)
      : ReadCallback(wcb)
      , base_(base) {}

  // Do not write data back, terminate the loop.
  void readDataAvailable(size_t len) noexcept override {
    std::cerr << "readDataAvailable, len " << len << std::endl;

    currentBuffer.length = len;

    buffers.push_back(currentBuffer);
    currentBuffer.reset();
    state = STATE_SUCCEEDED;

    socket_->setReadCB(nullptr);
    base_->terminateLoopSoon();
  }
 private:
  EventBase* base_;
};


/**
 * Test a full unencrypted codepath
 */
TEST(AsyncSSLSocketTest, UnencryptedTest) {
  EventBase base;

  auto clientCtx = std::make_shared<folly::SSLContext>();
  auto serverCtx = std::make_shared<folly::SSLContext>();
  int fds[2];
  getfds(fds);
  getctx(clientCtx, serverCtx);
  auto client = AsyncSSLSocket::newSocket(
                  clientCtx, &base, fds[0], false, true);
  auto server = AsyncSSLSocket::newSocket(
                  serverCtx, &base, fds[1], true, true);

  ReadCallbackTerminator readCallback(&base, nullptr);
  server->setReadCB(&readCallback);
  readCallback.setSocket(server);

  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  client->write(nullptr, buf, sizeof(buf));

  // Check that bytes are unencrypted
  char c;
  EXPECT_EQ(1, recv(fds[1], &c, 1, MSG_PEEK));
  EXPECT_EQ('a', c);

  EventBaseAborter eba(&base, 3000);
  base.loop();

  EXPECT_EQ(1, readCallback.buffers.size());
  EXPECT_EQ(AsyncSSLSocket::STATE_UNENCRYPTED, client->getSSLState());

  server->setReadCB(&readCallback);

  // Unencrypted
  server->sslAccept(nullptr);
  client->sslConn(nullptr);

  // Do NOT wait for handshake, writing should be queued and happen after

  client->write(nullptr, buf, sizeof(buf));

  // Check that bytes are *not* unencrypted
  char c2;
  EXPECT_EQ(1, recv(fds[1], &c2, 1, MSG_PEEK));
  EXPECT_NE('a', c2);


  base.loop();

  EXPECT_EQ(2, readCallback.buffers.size());
  EXPECT_EQ(AsyncSSLSocket::STATE_ESTABLISHED, client->getSSLState());
}

} // namespace

///////////////////////////////////////////////////////////////////////////
// init_unit_test_suite
///////////////////////////////////////////////////////////////////////////
namespace {
struct Initializer {
  Initializer() {
    signal(SIGPIPE, SIG_IGN);
  }
};
Initializer initializer;
} // anonymous
