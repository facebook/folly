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

#include <folly/FileUtil.h>
#include <folly/io/async/SSLContext.h>
#include <folly/portability/GTest.h>
#include <folly/testing/TestUtil.h>

using namespace folly;
using namespace folly::test;
/*
 * This test is meant to verify that SSLContext correctly sets its minimum
 * protocol version and is not blocked by OpenSSL's default config.
 */

/*
 * The default OpenSSL config file contents for version OpenSSL 1.1.1c FIPS  28
 * May 2019. This should set the MinProtocol to TLS 1.2 and SECLEVEL to 2.
 */
const std::string kOpenSSLConf = folly::stripLeftMargin(R"(
    CipherString = @SECLEVEL=2:kEECDH:kRSA:kEDH:kPSK:kDHEPSK:kECDHEPSK:-aDSS:-3DES:!DES:!RC4:!RC2:!IDEA:-SEED:!eNULL:!aNULL:!MD5:-SHA384:-CAMELLIA:-ARIA:-AESCCM8
    Ciphersuites = TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:TLS_AES_128_CCM_SHA256
    MinProtocol = TLSv1.2
)");

class SSLContextRegressionTest : public testing::Test {
 public:
  void SetUp() override {
    TemporaryFile confFile{};
    writeFile(StringPiece(kOpenSSLConf), confFile.path().c_str());

    // simulate the system environment by loading a config file that should
    // represent the CentOS 8 configuration
    int result = CONF_modules_load_file(confFile.path().c_str(), nullptr, 0);
    ASSERT_EQ(result, 1) << "Failed to load OpenSSL conf from temporary file.";
  }
};

// Tests that specifying a TLS version actually sets the underlying SSL
// context's minimum to that same version (and not the default in the config).
TEST_F(SSLContextRegressionTest, IsNotAffectedBySystemEnvironment) {
  auto ctx = std::make_shared<SSLContext>(SSLContext::SSLVersion::TLSv1);
  ASSERT_EQ(SSL_CTX_get_min_proto_version(ctx->getSSLCtx()), TLS1_VERSION);
}
