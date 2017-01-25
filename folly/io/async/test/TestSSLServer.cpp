/*
 * Copyright 2017 Facebook, Inc.
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
#include <folly/io/async/test/TestSSLServer.h>

namespace folly {

const char* kTestCert = "folly/io/async/test/certs/tests-cert.pem";
const char* kTestKey = "folly/io/async/test/certs/tests-key.pem";
const char* kTestCA = "folly/io/async/test/certs/ca-cert.pem";

TestSSLServer::TestSSLServer(SSLServerAcceptCallbackBase* acb, bool enableTFO)
    : ctx_(new SSLContext),
      acb_(acb),
      socket_(AsyncServerSocket::newSocket(&evb_)) {
  // Set up the SSL context
  ctx_->loadCertificate(kTestCert);
  ctx_->loadPrivateKey(kTestKey);
  ctx_->ciphers("ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

  acb_->ctx_ = ctx_;
  acb_->base_ = &evb_;

  // Enable TFO
  if (enableTFO) {
    LOG(INFO) << "server TFO enabled";
    socket_->setTFOEnabled(true, 1000);
  }

  // set up the listening socket
  socket_->bind(0);
  socket_->getAddress(&address_);
  socket_->listen(100);
  socket_->addAcceptCallback(acb_, &evb_);
  socket_->startAccepting();

  thread_ = std::thread([&] { Main(); });
  LOG(INFO) << "Accepting connections on " << address_;
}

TestSSLServer::~TestSSLServer() {
  if (thread_.joinable()) {
    evb_.runInEventBaseThread([&]() { socket_->stopAccepting(); });
    LOG(INFO) << "Waiting for server thread to exit";
    thread_.join();
  }
}
}
