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

#pragma once

#include <boost/variant.hpp>

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSession.h>

namespace folly {

class SSLContext;

namespace ssl {

namespace detail {

class OpenSSLSession;

} // namespace detail

/**
 * A class that manages one SSL session.
 *
 * Only intended to temporarily handle both raw session and
 * abstract session types, until session APIs are merged in
 * in AsyncSSLSocket. Afterwards, it will only manage an
 * abstract session.
 */
class SSLSessionManager {
 public:
  SSLSessionManager();

  ~SSLSessionManager() = default;

  void setSession(std::shared_ptr<folly::ssl::SSLSession> session);

  std::shared_ptr<folly::ssl::SSLSession> getSession() const;

  void setRawSession(folly::ssl::SSLSessionUniquePtr session);

  folly::ssl::SSLSessionUniquePtr getRawSession() const;

  /**
   * Add SSLSessionManager instance to the ex data of ssl.
   * Needs to be called for SSLSessionManager::getFromSSL to return
   * a non-null pointer.
   */
  void attachToSSL(SSL* ssl);

  /**
   * Get pointer to a SSLSessionManager instance that was added to
   * the ex data of ssl through attachToSSL()
   */
  static SSLSessionManager* getFromSSL(const SSL* ssl);

 private:
  friend class folly::SSLContext;

  /**
   * Called by SSLContext when a new session is negotiated for the
   * SSL connection that SSLSessionManager is attached to.
   */
  void onNewSession(folly::ssl::SSLSessionUniquePtr session);

  /**
   * The SSL session. Which type the variant contains depends on the
   * session API that is used.
   */
  boost::variant<
      folly::ssl::SSLSessionUniquePtr,
      std::shared_ptr<folly::ssl::detail::OpenSSLSession>>
      session_;
};

} // namespace ssl
} // namespace folly
