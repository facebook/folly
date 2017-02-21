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
#pragma once

#include <string>
#include <vector>

#include <openssl/x509.h>

#include <folly/Optional.h>

namespace folly {
namespace ssl {

class OpenSSLCertUtils {
 public:
  // Note: non-const until OpenSSL 1.1.0
  static Optional<std::string> getCommonName(X509& x509);

  static std::vector<std::string> getSubjectAltNames(X509& x509);
};
}
}
