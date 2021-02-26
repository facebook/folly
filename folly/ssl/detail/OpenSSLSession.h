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

#include <folly/SharedMutex.h>
#include <folly/Synchronized.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSession.h>

namespace folly {
namespace ssl {
namespace detail {

/**
 * Internal implementation detail.
 *
 * This class should generally not be
 * directly, but instead downcast from SSLSession only when
 * access is needed to OpenSSL's SSL_SESSION (e.g during resumption).
 *
 * See also SSLSession.h for more information.
 */

class OpenSSLSession : public SSLSession {
 public:
  ~OpenSSLSession() = default;

  /**
   * Set the underlying SSL session. Any previously held session
   * will have its reference count decremented by 1.
   */
  void setActiveSession(SSLSessionUniquePtr s);

  /*
   * Get the underlying SSL session.
   */
  SSLSessionUniquePtr getActiveSession();

 private:
  folly::Synchronized<SSLSessionUniquePtr, folly::SharedMutex> activeSession_;
};

} // namespace detail
} // namespace ssl
} // namespace folly
