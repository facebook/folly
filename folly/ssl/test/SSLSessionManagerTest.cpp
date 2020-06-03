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

#include <gtest/gtest.h>

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSessionManager.h>
#include <folly/ssl/detail/OpenSSLSession.h>

using folly::ssl::SSLSessionManager;
using folly::ssl::SSLSessionUniquePtr;
using folly::ssl::detail::OpenSSLSession;

namespace folly {

TEST(SSLSessionManagerTest, InitialStateTest) {
  SSLSessionManager manager;
  EXPECT_EQ(nullptr, manager.getRawSession().get());
  EXPECT_NE(nullptr, manager.getSession());
}

TEST(SSLSessionManagerTest, SetSessionTest) {
  SSLSessionManager manager;

  auto newSession = std::make_shared<OpenSSLSession>();
  manager.setSession(newSession);
  EXPECT_EQ(nullptr, manager.getRawSession().get());
  EXPECT_EQ(newSession, manager.getSession());
  EXPECT_EQ(newSession.get(), manager.getSession().get());

  manager.setSession(nullptr);
  EXPECT_EQ(nullptr, manager.getRawSession().get());
  EXPECT_EQ(nullptr, manager.getSession());
  EXPECT_EQ(nullptr, manager.getSession().get());
}

TEST(SSLSessionManagerTest, SetRawSesionTest) {
  SSLSessionManager manager;

  auto sslSession = SSL_SESSION_new();
  SSL_SESSION_up_ref(sslSession);
  auto newSession = SSLSessionUniquePtr(sslSession);
  manager.setRawSession(std::move(newSession));
  EXPECT_EQ(nullptr, manager.getSession());
  EXPECT_EQ(sslSession, manager.getRawSession().get());
  SSL_SESSION_free(sslSession);

  manager.setRawSession(nullptr);
  EXPECT_EQ(nullptr, manager.getSession());
  EXPECT_EQ(nullptr, manager.getRawSession().get());
}

TEST(SSLSessionManagerTest, GetFromSSLTest) {
  SSLSessionManager manager;
  SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());

  SSL* ssl1 = SSL_new(ctx);
  EXPECT_EQ(nullptr, SSLSessionManager::getFromSSL(ssl1));
  SSL_free(ssl1);

  SSL* ssl2 = SSL_new(ctx);
  manager.attachToSSL(ssl2);
  EXPECT_EQ(&manager, SSLSessionManager::getFromSSL(ssl2));
  SSL_free(ssl2);

  SSL_CTX_free(ctx);
}

} // namespace folly
