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

#include <folly/io/async/ssl/OpenSSLUtils.h>
#include <folly/String.h>
#include <folly/portability/GTest.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

using namespace ::testing;
using namespace folly::ssl;

namespace folly {

const std::string kSampleCommonName = "Folly Library";

// a certificate with a CN that uses 64 characters, the max length
const std::string kSampleCommonNameMaxLength =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// create and return an X509 object with only the subject's CN set, so tests can
// extract and compare it
X509UniquePtr createMinimalX509(const std::string& commonName) {
  X509* x509;
  x509 = X509_new();
  X509_NAME* name;
  name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(
      name,
      SN_commonName,
      MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>(commonName.data()),
      -1,
      -1,
      0);

  return X509UniquePtr(x509);
}

// Tests that the common name is extracted from the x509 certificate with the
// correct length
TEST(OpenSSLUtilsTest, getCommonName) {
  X509UniquePtr x509 = createMinimalX509(kSampleCommonName);

  EXPECT_EQ(OpenSSLUtils::getCommonName(x509.get()), kSampleCommonName);
}

// Tests that the common name is extracted from the x509 certificate correctly
// when its length is the maximum, defined as ub_common_name in asn1.h (RFC2459)
TEST(OpenSSLUtilsTest, getCommonNameMaxLength) {
  X509UniquePtr x509 = createMinimalX509(kSampleCommonNameMaxLength);

  // read common name from certificate
  EXPECT_EQ(
      OpenSSLUtils::getCommonName(x509.get()), kSampleCommonNameMaxLength);
}

// Tests that getCommonName returns an empty string for a null X509 argument
TEST(OpenSSLUtilsTest, getCommonNameNullX509) {
  EXPECT_EQ(OpenSSLUtils::getCommonName(nullptr), "");
}

// Tests that getCommonName returns an empty string because the given
// certificate has no CN
TEST(OpenSSLUtilsTest, getCommonNameEmpty) {
  X509UniquePtr x509 = createMinimalX509("");

  EXPECT_EQ(OpenSSLUtils::getCommonName(x509.get()), "");
}

} // namespace folly
