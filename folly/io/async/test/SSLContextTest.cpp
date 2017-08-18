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

#include <folly/io/async/SSLContext.h>
#include <folly/portability/GTest.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

using namespace std;
using namespace testing;

namespace folly {

class SSLContextTest : public testing::Test {
 public:
  SSLContext ctx;
  void verifySSLCipherList(const vector<string>& ciphers);
};

void SSLContextTest::verifySSLCipherList(const vector<string>& ciphers) {
  int i = 0;
  ssl::SSLUniquePtr ssl(ctx.createSSL());
  for (auto& cipher : ciphers) {
    ASSERT_STREQ(cipher.c_str(), SSL_get_cipher_list(ssl.get(), i++));
  }
  ASSERT_EQ(nullptr, SSL_get_cipher_list(ssl.get(), i));
}

TEST_F(SSLContextTest, TestSetCipherString) {
  ctx.ciphers("AES128-SHA:ECDHE-RSA-AES256-SHA384");
  verifySSLCipherList({"AES128-SHA", "ECDHE-RSA-AES256-SHA384"});
}

TEST_F(SSLContextTest, TestSetCipherList) {
  const vector<string> ciphers = {"ECDHE-RSA-AES128-SHA", "AES256-SHA"};
  ctx.setCipherList(ciphers);
  verifySSLCipherList(ciphers);
}
}
