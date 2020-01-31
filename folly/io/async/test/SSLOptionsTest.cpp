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

#include <folly/io/async/SSLOptions.h>
#include <folly/io/async/SSLContext.h>
#include <folly/portability/GTest.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

#include <folly/io/async/test/SSLUtil.h>

using namespace std;

namespace folly {

class SSLOptionsTest : public testing::Test {};

TEST_F(SSLOptionsTest, TestSetCommonCipherList) {
  SSLContext ctx;
  ssl::setCipherSuites<ssl::SSLCommonOptions>(ctx);

  const auto& commonOptionCiphers = ssl::SSLCommonOptions::ciphers();
  std::vector<std::string> commonOptionCiphersVec(
      begin(commonOptionCiphers), end(commonOptionCiphers));

  ssl::SSLUniquePtr ssl(ctx.createSSL());
  EXPECT_EQ(commonOptionCiphersVec, test::getNonTLS13CipherList(ssl.get()));
}

TEST_F(SSLOptionsTest, TestSetCipherListWithVector) {
  SSLContext ctx;
  auto ciphers = ssl::SSLCommonOptions::ciphers();
  ssl::setCipherSuites(ctx, ciphers);

  ssl::SSLUniquePtr ssl(ctx.createSSL());
  std::vector<std::string> expectedCiphers(begin(ciphers), end(ciphers));
  EXPECT_EQ(expectedCiphers, test::getNonTLS13CipherList(ssl.get()));
}

} // namespace folly
