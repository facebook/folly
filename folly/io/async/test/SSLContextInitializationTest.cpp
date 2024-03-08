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

#include <functional>

#include <folly/init/Init.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/test/AsyncSSLSocketTest.h>
#include <folly/portability/GTest.h>
#include <folly/ssl/Init.h>

namespace folly {

void setupSSLLocks() {
  folly::ssl::setLockTypes({
#ifdef CRYPTO_LOCK_EVP_PKEY
      {CRYPTO_LOCK_EVP_PKEY, folly::ssl::LockType::NONE},
#endif
#ifdef CRYPTO_LOCK_SSL_SESSION
      {CRYPTO_LOCK_SSL_SESSION, folly::ssl::LockType::SPINLOCK},
#endif
#ifdef CRYPTO_LOCK_SSL_CTX
      {CRYPTO_LOCK_SSL_CTX, folly::ssl::LockType::NONE}
#endif
  });
}

TEST(SSLContextInitializationTest, SSLContextInitializeThenSetLocksAndInit) {
  EXPECT_DEATH(
      {
        folly::ssl::init();
        folly::ssl::setLockTypesAndInit({});
      },
      "OpenSSL is already initialized");
}

TEST(SSLContextInitializationTest, SSLContextSetLocksAndInitialize) {
  EXPECT_DEATH(
      {
        folly::ssl::setLockTypesAndInit({});
        folly::ssl::setLockTypesAndInit({});
      },
      "OpenSSL is already initialized");
}

TEST(SSLContextInitializationTest, SSLContextLocks) {
  EXPECT_EXIT(
      {
        setupSSLLocks();
        folly::ssl::init();
#ifdef CRYPTO_LOCK_EVP_PKEY
        EXPECT_TRUE(folly::ssl::isLockDisabled(CRYPTO_LOCK_EVP_PKEY));
#endif
#ifdef CRYPTO_LOCK_SSL_SESSION
        EXPECT_FALSE(folly::ssl::isLockDisabled(CRYPTO_LOCK_SSL_SESSION));
#endif
#ifdef CRYPTO_LOCK_ERR
        EXPECT_FALSE(folly::ssl::isLockDisabled(CRYPTO_LOCK_ERR));
#endif
        if (::testing::Test::HasFailure()) {
          exit(1);
        }
        LOG(INFO) << "SSLContextLocks passed";
        exit(0);
      },
      ::testing::ExitedWithCode(0),
      "SSLContextLocks passed");
}

TEST(SSLContextInitializationTest, SSLContextLocksSetAfterInitIgnored) {
  EXPECT_EXIT(
      {
        setupSSLLocks();
        folly::ssl::init();
        folly::ssl::setLockTypes({});
#ifdef CRYPTO_LOCK_EVP_PKEY
        EXPECT_TRUE(folly::ssl::isLockDisabled(CRYPTO_LOCK_EVP_PKEY));
#endif
        if (::testing::Test::HasFailure()) {
          exit(1);
        }
        LOG(INFO) << "SSLContextLocksSetAfterInitIgnored passed";
        exit(0);
      },
      ::testing::ExitedWithCode(0),
      "SSLContextLocksSetAfterInitIgnored passed");
}

TEST(SSLContextInitializationTest, SSLContextSslCtxConstructor) {
  folly::ssl::init();

  // Used to determine when SSL_CTX is freed.
  auto onFree = [](void*, void* arg, CRYPTO_EX_DATA*, int, long, void*) {
    bool* freed = static_cast<bool*>(arg);
    *freed = true;
  };
  static int idx = SSL_CTX_get_ex_new_index(
      0 /*argl */,
      nullptr /*argp*/,
      nullptr /*new_func*/,
      nullptr /*dup_func*/,
      onFree /*free_func*/);

  bool freed = false;
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
  EXPECT_NE(ctx, nullptr) << "SSL_CTX* creation for test failed";

  (void)SSL_CTX_set_ex_data(ctx, idx, &freed);

  {
    // SSLContext takes "ownership" (read: increments the ref count), and will
    // free ctx on destruction.
    folly::SSLContext sslContext(ctx);
  }
  // Shouldn't be fully freed because SSLContext should've added to the
  // refcount. up_ref should succed
  EXPECT_EQ(freed, false);

  // Last reference, ctx should no longer be valid. Should trigger the ex_data
  // free func.
  SSL_CTX_free(ctx);
  EXPECT_EQ(freed, true);
}

} // namespace folly

int main(int argc, char* argv[]) {
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);

  return RUN_ALL_TESTS();
}
