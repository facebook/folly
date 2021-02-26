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

#include <folly/SharedMutex.h>
#include <folly/Synchronized.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace ssl {
namespace detail {

void OpenSSLSession::setActiveSession(SSLSessionUniquePtr s) {
  // OpenSSLSession is typically shared as a std::shared_ptr<SSLSession>,
  // and setActiveSession() may be invoked in mulitple threads. Consequently,
  // changing the `activeSession_ pointer needs to be synchronized,
  // such that readers are able to fully acquire a reference count in
  // getActiveSession().

  activeSession_.withWLock([&](auto& sessionPtr) { sessionPtr.swap(s); });
}

SSLSessionUniquePtr OpenSSLSession::getActiveSession() {
  return activeSession_.withRLock([](auto& sessionPtr) {
    SSL_SESSION* session = sessionPtr.get();
    if (session) {
      SSL_SESSION_up_ref(session);
    }
    return SSLSessionUniquePtr(session);
  });
}

} // namespace detail
} // namespace ssl
} // namespace folly
