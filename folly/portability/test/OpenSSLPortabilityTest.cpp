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

#include <folly/portability/GTest.h>
#include <folly/portability/OpenSSL.h>

using namespace folly;
using namespace testing;

TEST(OpenSSLPortabilityTest, TestRSASetter) {
  RSA* r = RSA_new();
  RSA* public_key = RSA_new();
  BIGNUM* n = BN_new();
  BIGNUM* e = BN_new();
  BIGNUM* d = BN_new();
  const BIGNUM* n_actual = BN_new();
  const BIGNUM* e_actual = BN_new();
  const BIGNUM* d_actual = BN_new();
  EXPECT_TRUE(BN_set_bit(n, 1));
  EXPECT_TRUE(BN_set_bit(e, 3));
  EXPECT_TRUE(BN_set_bit(d, 2));
  RSA_set0_key(r, n, e, d);
  RSA_get0_key(r, &n_actual, &e_actual, &d_actual);
  // BN_cmp returns 0 if the two BIGNUMs are equal
  EXPECT_FALSE(BN_cmp(n, n_actual));
  EXPECT_FALSE(BN_cmp(e, e_actual));
  EXPECT_FALSE(BN_cmp(d, d_actual));

  RSA_set0_key(public_key, n, e, nullptr);
  const BIGNUM* n_public = BN_new();
  const BIGNUM* e_public = BN_new();
  RSA_get0_key(public_key, &n_public, &e_public, nullptr);
  EXPECT_FALSE(BN_cmp(n, n_public));
  EXPECT_FALSE(BN_cmp(e, e_public));
}
