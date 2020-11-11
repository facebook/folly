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

#include <folly/Try.h>
#include <folly/Unit.h>
#include <folly/io/async/AsyncTransportCertificate.h>

namespace folly {

/**
 * CertificateIdentityVerifier implementations are used during TLS handshakes to
 * extract and verify end-entity certificate identities. AsyncSSLSocket first
 * performs OpenSSL certificate chain validation and then invokes any registered
 * HandshakeCB's handshakeVer() method. Only if both of these succeed, it then
 * calls the verifyLeaf method to further verify a peer's certificate.
 *
 * TLS connections must pass all these checks in order for an AsyncSSLSocket's
 * registered HandshakeCB to receive handshakeSuc().
 *
 * Instances can be shared across AsyncSSLSockets so implementations should be
 * thread-safe.
 */
class CertificateIdentityVerifier {
 public:
  virtual ~CertificateIdentityVerifier() = default;

  /**
   * AsyncSSLSocket calls verifyLeaf() during a TLS handshake after chain
   * verification, only if certificate verification is required/requested,
   * with the peer's leaf certificate provided as an argument.
   *
   * Returns an Try with a CertificateIdentityVerifierException set if
   * verification fails.
   *
   * @param leafCertificate leaf X509 certificate of the connected peer
   */
  FOLLY_NODISCARD virtual Try<Unit> verifyLeaf(
      const AsyncTransportCertificate& leafCertificate) const noexcept = 0;
};

/**
 * Base of exception hierarchy for CertificateIdentityVerifier failure reasons.
 */
class FOLLY_EXPORT CertificateIdentityVerifierException
    : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

} // namespace folly
