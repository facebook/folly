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

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace test {

static std::vector<std::string> getCiphersFromSSL(SSL* s) {
  std::vector<std::string> osslCiphers;
  for (int i = 0;; i++) {
    auto c = SSL_get_cipher_list(s, i);
    if (c == nullptr) {
      break;
    }
    osslCiphers.emplace_back(std::move(c));
  }

  return osslCiphers;
}

static std::vector<std::string> getNonTLS13CipherList(SSL* s) {
  // OpenSSL 1.1.1 added TLS 1.3 support; SSL_get_cipher_list will return
  // TLS 1.3 ciphersuites, even if they are not explicitly added by
  // SSL_CTX_set_cipher_list (This is to ensure that applications built against
  // 1.1.0 which did not have TLS 1.3 support would still be able to
  // transparently begin negotiating TLS 1.3).
  //
  // For the time being, filter out any TLS 1.3 ciphers that appear in the
  // returned list. A more accurate test would be to determine at runtime
  // if TLS 1.3 support is available, and assert that the TLS 1.3 ciphersuites
  // are added regardless of what we put in SSL_CTX_set_cipher_list
  static std::set<std::string> const suitesFor13{
      "TLS_AES_256_GCM_SHA384",
      "TLS_CHACHA20_POLY1305_SHA256",
      "TLS_AES_128_GCM_SHA256",
  };

  auto ciphers = getCiphersFromSSL(s);
  ciphers.erase(
      std::remove_if(
          begin(ciphers),
          end(ciphers),
          [](const std::string& cipher) {
            return suitesFor13.count(cipher) > 0;
          }),
      end(ciphers));
  return ciphers;
}

} // namespace test
} // namespace folly
