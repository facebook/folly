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

#pragma once

#include <folly/io/async/AsyncTransportCertificate.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {

/**
 * Generic interface applications may implement to convey self or peer
 * certificate related information.
 */
class OpenSSLTransportCertificate : virtual public AsyncTransportCertificate {
 public:
  virtual ~OpenSSLTransportCertificate() override = default;

  /**
   * Returns an X509 structure associated with this Certificate. This may be
   * null.
   */
  virtual folly::ssl::X509UniquePtr getX509() const = 0;

  static ssl::X509UniquePtr tryExtractX509(
      const AsyncTransportCertificate* cert) {
    auto opensslCert = dynamic_cast<const OpenSSLTransportCertificate*>(cert);
    return opensslCert ? opensslCert->getX509() : nullptr;
  }
};
} // namespace folly
