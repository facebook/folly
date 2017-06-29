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
#include <folly/ssl/OpenSSLPtrTypes.h>

using namespace folly;
using namespace folly::ssl;
using namespace testing;

TEST(OpenSSLPortabilityTest, TestRSASetter) {
  RsaUniquePtr r(RSA_new());
  BIGNUM* n = BN_new();
  BIGNUM* e = BN_new();
  BIGNUM* d = BN_new();
  BIGNUM* n_actual;
  BIGNUM* e_actual;
  BIGNUM* d_actual;
  EXPECT_TRUE(BN_set_bit(n, 1));
  EXPECT_TRUE(BN_set_bit(e, 3));
  EXPECT_TRUE(BN_set_bit(d, 2));
  RSA_set0_key(r.get(), n, e, d);
  RSA_get0_key(
      r.get(),
      (const BIGNUM**)&n_actual,
      (const BIGNUM**)&e_actual,
      (const BIGNUM**)&d_actual);
  // BN_cmp returns 0 if the two BIGNUMs are equal
  EXPECT_FALSE(BN_cmp(n, n_actual));
  EXPECT_FALSE(BN_cmp(e, e_actual));
  EXPECT_FALSE(BN_cmp(d, d_actual));

  RsaUniquePtr public_key(RSA_new());
  BIGNUM* n_public = BN_new();
  BIGNUM* e_public = BN_new();
  EXPECT_TRUE(BN_set_bit(n_public, 1));
  EXPECT_TRUE(BN_set_bit(e_public, 3));
  RSA_set0_key(public_key.get(), n_public, e_public, nullptr);
  BIGNUM* n_public_actual;
  BIGNUM* e_public_actual;
  RSA_get0_key(
      public_key.get(),
      (const BIGNUM**)&n_public_actual,
      (const BIGNUM**)&e_public_actual,
      nullptr);
  EXPECT_FALSE(BN_cmp(n_public, n_public_actual));
  EXPECT_FALSE(BN_cmp(e_public, e_public_actual));
}
