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

#include <folly/ssl/SSLSessionManager.h>

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/detail/OpenSSLSession.h>

using folly::ssl::SSLSessionUniquePtr;
using folly::ssl::detail::OpenSSLSession;
using std::shared_ptr;

namespace {

/**
 * Variant visitors. Will be removed once session_ is converted
 * to a non-variant type.
 */
class RawSessionRetrievalVisitor {
 public:
  SSLSessionUniquePtr operator()(const SSLSessionUniquePtr& sessionPtr) const {
    SSL_SESSION* session = sessionPtr.get();
    if (session) {
      SSL_SESSION_up_ref(session);
    }
    return SSLSessionUniquePtr(session);
  }

  SSLSessionUniquePtr operator()(
      const shared_ptr<OpenSSLSession>& session) const {
    if (!session) {
      return SSLSessionUniquePtr();
    }

    return session->getActiveSession();
  }
};

class SSLSessionRetrievalVisitor {
 public:
  shared_ptr<OpenSSLSession> operator()(const SSLSessionUniquePtr&) const {
    return nullptr;
  }

  shared_ptr<OpenSSLSession> operator()(
      const shared_ptr<OpenSSLSession>& session) const {
    return session;
  }
};

class SessionForwarderVisitor {
 public:
  explicit SessionForwarderVisitor(SSLSessionUniquePtr sessionArg)
      : sessionArg_{std::move(sessionArg)} {}

  void operator()(const SSLSessionUniquePtr&) {}

  void operator()(const std::shared_ptr<OpenSSLSession>& session) {
    if (session) {
      session->setActiveSession(std::move(sessionArg_));
    }
  }

 private:
  SSLSessionUniquePtr sessionArg_{nullptr};
};

int getSSLExDataIndex() {
  static auto index =
      SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  return index;
}

} // namespace

namespace folly {
namespace ssl {

SSLSessionManager::SSLSessionManager() {
  session_ = std::make_shared<OpenSSLSession>();
}

void SSLSessionManager::setSession(shared_ptr<SSLSession> session) {
  if (session == nullptr) {
    session_ = std::shared_ptr<OpenSSLSession>(nullptr);
    return;
  }

  auto openSSLSession = std::dynamic_pointer_cast<OpenSSLSession>(session);
  if (openSSLSession) {
    session_ = openSSLSession;
  }
}

void SSLSessionManager::setRawSession(SSLSessionUniquePtr session) {
  session_ = std::move(session);
}

SSLSessionUniquePtr SSLSessionManager::getRawSession() const {
  auto visitor = RawSessionRetrievalVisitor();
  return std::visit(visitor, session_);
}

shared_ptr<SSLSession> SSLSessionManager::getSession() const {
  auto visitor = SSLSessionRetrievalVisitor();
  return std::visit(visitor, session_);
}

void SSLSessionManager::attachToSSL(SSL* ssl) {
  SSL_set_ex_data(ssl, getSSLExDataIndex(), this);
}

SSLSessionManager* SSLSessionManager::getFromSSL(const SSL* ssl) {
  return static_cast<SSLSessionManager*>(
      SSL_get_ex_data(ssl, getSSLExDataIndex()));
}

void SSLSessionManager::onNewSession(SSLSessionUniquePtr session) {
  auto visitor = SessionForwarderVisitor(std::move(session));
  std::visit(visitor, session_);
}

} // namespace ssl
} // namespace folly
