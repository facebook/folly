/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace ssl {

class OpenSSLCertUtils {
 public:
  // Note: non-const until OpenSSL 1.1.0
  static Optional<std::string> getCommonName(X509& x509);

  static std::vector<std::string> getSubjectAltNames(X509& x509);

  /*
   * Return the subject name, if any, from the cert
   * @param x509    Reference to an X509
   * @return a folly::Optional<std::string>, or folly::none
   */
  static Optional<std::string> getSubject(X509& x509);

  /*
   * Return the issuer name, if any, from the cert
   * @param x509    Reference to an X509
   * @return a folly::Optional<std::string>, or folly::none
   */
  static Optional<std::string> getIssuer(X509& x509);

  /*
   * Get a string representation of the not-before time on the certificate
   */
  static std::string getNotBeforeTime(X509& x509);

  /*
   * Get a string representation of the not-after (expiration) time
   */
  static std::string getNotAfterTime(X509& x509);

  /*
   * Summarize the CN, Subject, Issuer, Validity, and extensions as a string
   */
  static folly::Optional<std::string> toString(X509& x509);

  /**
   * Decode the DER representation of an X509 certificate.
   *
   * Throws on error (if a valid certificate can't be decoded).
   */
  static X509UniquePtr derDecode(ByteRange);

  /**
   * Encode an X509 certificate in DER format.
   *
   * Throws on error.
   */
  static std::unique_ptr<IOBuf> derEncode(X509&);

  /**
   * Read certificates from memory and returns them as a vector of X509
   * pointers. Throw if there is any malformed cert or memory allocation
   * problem.
   * @param range Buffer to parse.
   * @return A vector of X509 objects.
   */
  static std::vector<X509UniquePtr> readCertsFromBuffer(ByteRange range);

  /**
   * Return the output of the X509_digest for chosen message-digest algo
   * NOTE: The returned digest will be in binary, and may need to be
   * hex-encoded
   */
  static std::array<uint8_t, SHA_DIGEST_LENGTH> getDigestSha1(X509& x509);
  static std::array<uint8_t, SHA256_DIGEST_LENGTH> getDigestSha256(X509& x509);

  /**
   * Read a store from a file. Throw if unable to read the file, memory
   * allocation fails, or any cert can't be parsed or added to the store.
   * @param caFile Path to the CA file.
   * @return A X509 store that contains certs in the CA file.
   */
  static X509StoreUniquePtr readStoreFromFile(std::string caFile);

  /**
   * Read a store from a PEM buffer. Throw if memory allocation fails, or
   * any cert can't be parsed or added to the store.
   * @param range A buffer containing certs in PEM format.
   * @return A X509 store that contains certs in the CA file.
   */
  static X509StoreUniquePtr readStoreFromBuffer(ByteRange range);

 private:
  static std::string getDateTimeStr(const ASN1_TIME* time);
};
} // namespace ssl
} // namespace folly
