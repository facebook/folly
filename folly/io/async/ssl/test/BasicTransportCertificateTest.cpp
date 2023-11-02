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

#include <folly/io/async/ssl/BasicTransportCertificate.h>

#include <folly/FileUtil.h>
#include <folly/experimental/TestUtil.h>
#include <folly/portability/GTest.h>
#include <folly/ssl/Init.h>
#include <folly/ssl/OpenSSLCertUtils.h>

using namespace folly;
using namespace folly::ssl;
using folly::test::find_resource;

const char* kTestCerts = "folly/io/async/ssl/test/tests-cert.pem";

TEST(BasicTransportCertificateTest, TestCerts) {
  folly::ssl::init();
  auto path = find_resource(kTestCerts);
  std::string certData;
  EXPECT_TRUE(folly::readFile(path.c_str(), certData));
  auto certs = OpenSSLCertUtils::readCertsFromBuffer(StringPiece(certData));
  EXPECT_FALSE(certs.empty());
  auto x509Ptr = std::move(certs[0]);
  EXPECT_NE(x509Ptr, nullptr);

  {
    SCOPED_TRACE("create w/ null");
    auto cert = BasicTransportCertificate::create(nullptr);
    EXPECT_EQ(cert, nullptr);
  }

  {
    SCOPED_TRACE("construct with empty cert");
    BasicTransportCertificate cert("foo", nullptr);
    EXPECT_EQ(cert.getX509(), nullptr);
    EXPECT_EQ(cert.getIdentity(), "foo");
    auto cloned = BasicTransportCertificate::create(&cert);
    EXPECT_EQ(cloned->getX509(), nullptr);
    EXPECT_EQ(cloned->getIdentity(), "foo");
  }

  {
    SCOPED_TRACE("construct w/ x509");
    auto x509Raw = x509Ptr.get();
    EXPECT_NE(x509Raw, nullptr);
    BasicTransportCertificate cert("x509", std::move(x509Ptr));
    EXPECT_EQ(cert.getX509().get(), x509Raw);
    EXPECT_EQ(cert.getIdentity(), "x509");

    auto cloned = BasicTransportCertificate::create(&cert);
    EXPECT_EQ(cloned->getX509().get(), x509Raw);
    EXPECT_EQ(cloned->getIdentity(), "x509");
  }
}
