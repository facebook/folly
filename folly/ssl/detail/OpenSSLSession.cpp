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

#include <folly/ssl/detail/OpenSSLSession.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <atomic>

namespace folly {
namespace ssl {
namespace detail {

OpenSSLSession::~OpenSSLSession() {
  SSL_SESSION* session = activeSession_.load();
  if (session) {
    SSL_SESSION_free(session);
  }
}

void OpenSSLSession::setActiveSession(SSLSessionUniquePtr s) {
  SSL_SESSION* oldSession = activeSession_.exchange(s.release());
  if (oldSession) {
    SSL_SESSION_free(oldSession);
  }
}

SSLSessionUniquePtr OpenSSLSession::getActiveSession() {
  SSL_SESSION* session = activeSession_.load();
  if (session) {
    SSL_SESSION_up_ref(session);
  }
  return SSLSessionUniquePtr(session);
}

} // namespace detail
} // namespace ssl
} // namespace folly
