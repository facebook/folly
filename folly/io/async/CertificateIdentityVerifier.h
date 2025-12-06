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

#include <memory>

#include <folly/io/async/AsyncTransportCertificate.h>
#include <folly/portability/OpenSSL.h>

namespace folly {

/**
 * CertificateIdentityVerifier implementations are used during TLS handshakes to
 * extract and verify end-entity certificate identities.
 *
 * During TLS handshake, AsyncSSLSocket performs verification in this order:
 * 1. OpenSSL performs internal certificate chain validation
 * 2. HandshakeCB's handshakeVer() is invoked (if registered)
 * 3. CertificateIdentityVerifier's verifyContext() is invoked (if registered)
 *    - Called once for EACH certificate in the chain (root → intermediate →
 * leaf)
 *    - Provides access to the full X509_STORE_CTX for chain inspection
 * 4. CertificateIdentityVerifier's verifyLeaf() is invoked (if registered)
 *    - Called ONLY for the leaf certificate at depth 0
 *    - Only if all previous verification steps succeeded
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
   * AsyncSSLSocket calls verifyContext() during the OpenSSL certificate
   * verification callback for EACH certificate in the chain (typically
   * root → intermediate(s) → leaf).
   *
   * This method allows custom validation logic beyond OpenSSL's internal
   * consistency checks. The returned value becomes the new verification state:
   * - If this method returns false, verification fails and verifyLeaf() will
   *   NOT be called
   * - If this method returns true, verification succeeds (and verifyLeaf()
   *   may be called for the leaf certificate at depth 0)
   *
   * This method can override OpenSSL's verification result. For example:
   * - If OpenSSL fails (preverifyOk=false) but verifyContext returns true,
   *   verification succeeds and verifyLeaf() may be called
   * - If OpenSSL succeeds (preverifyOk=true) but verifyContext returns false,
   *   verification fails and verifyLeaf() will NOT be called
   *
   * Unlike HandshakeCB::handshakeVer(), this method does not short-circuit
   * when it changes the result - it updates the verification state and allows
   * processing to continue.
   *
   * The default implementation returns preverifyOk unchanged (pass-through),
   * deferring to OpenSSL's verification decision.
   *
   * IMPORTANT: This method is called multiple times per handshake - once for
   * each certificate in the chain. Use X509_STORE_CTX_get_error_depth(ctx)
   * to determine the depth of the certificate being verified.
   *
   * See the passages on verify_callback in SSL_CTX_set_verify(3) for more
   * details.
   *
   * @param preverifyOk Result of OpenSSL's internal verification checks
   * @param ctx OpenSSL verification context containing the certificate chain
   *            and verification state. Use
   * X509_STORE_CTX_get_current_cert(&ctx) to access the certificate being
   * verified at this depth.
   * @return true if the certificate context is valid, false otherwise
   */
  virtual bool verifyContext(
      bool preverifyOk, [[maybe_unused]] X509_STORE_CTX* ctx) const noexcept {
    return preverifyOk;
  }

  /**
   * AsyncSSLSocket calls verifyLeaf() during a TLS handshake after chain
   * verification, only if certificate verification is required/requested,
   * with the peer's leaf certificate provided as an argument.
   *
   * Returns a pointer to AsyncTransportCertificate object.
   *
   * @param leafCertificate leaf X509 certificate of the connected peer
   */
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  virtual std::unique_ptr<AsyncTransportCertificate> verifyLeaf(
      const AsyncTransportCertificate& leafCertificate) const = 0;
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
