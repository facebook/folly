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

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/detail/OpenSSLThreading.h>

#include <stdexcept>

namespace folly {
namespace portability {
namespace ssl {

#ifdef OPENSSL_IS_BORINGSSL
int SSL_CTX_set1_sigalgs_list(SSL_CTX*, const char*) {
  return 1; // 0 implies error
}

int TLS1_get_client_version(SSL* s) {
  // Note that this isn't the client version, and the API to
  // get this has been hidden. It may be found by parsing the
  // ClientHello (there is a callback via the SSL_HANDSHAKE struct)
  return s->version;
}
#endif

} // namespace ssl
} // namespace portability
} // namespace folly
