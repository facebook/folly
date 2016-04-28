/*
 * Copyright 2016 Facebook, Inc.
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
#pragma once

#include <folly/Optional.h>
#include <folly/io/async/AsyncSocketException.h>

namespace folly {

enum class SSLError {
  CLIENT_RENEGOTIATION, // A client tried to renegotiate with this server
  INVALID_RENEGOTIATION, // We attempted to start a renegotiation.
  EARLY_WRITE, // Wrote before SSL connection established.
  // An openssl error type. The openssl specific methods should be used
  // to find the real error type.
  // This exists for compatibility until all error types can be move to proper
  // errors.
  OPENSSL_ERR,
};

class SSLException : public folly::AsyncSocketException {
 public:
  SSLException(
      int sslError,
      unsigned long errError,
      int sslOperationReturnValue,
      int errno_copy);

  explicit SSLException(SSLError error);

  SSLError getType() const {
    return sslError;
  }

  // These methods exist for compatibility until there are proper exceptions
  // for all ssl error types.
  int getOpensslSSLError() const {
    return opensslSSLError;
  }

  unsigned long getOpensslErr() const {
    return opensslErr;
  }

 private:
  SSLError sslError;
  int opensslSSLError;
  unsigned long opensslErr;
};
}
