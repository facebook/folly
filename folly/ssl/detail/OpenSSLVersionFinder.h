/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Conv.h>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

// BoringSSL doesn't have notion of versioning although it defines
// OPENSSL_VERSION_NUMBER to maintain compatibility. The following variables are
// intended to be specific to OpenSSL.
#if !defined(OPENSSL_IS_BORINGSSL)
# define OPENSSL_IS_101                       \
    (OPENSSL_VERSION_NUMBER >= 0x1000105fL && \
     OPENSSL_VERSION_NUMBER < 0x1000200fL)
# define OPENSSL_IS_102                       \
    (OPENSSL_VERSION_NUMBER >= 0x1000200fL && \
     OPENSSL_VERSION_NUMBER < 0x10100000L)
# define OPENSSL_IS_110 (OPENSSL_VERSION_NUMBER >= 0x10100000L)
#endif  // !defined(OPENSSL_IS_BORINGSSL)

// This is used to find the OpenSSL version at runtime. Just returning
// OPENSSL_VERSION_NUMBER is insufficient as runtime version may be different
// from the compile-time version
struct OpenSSLVersionFinder {
  static std::string getOpenSSLLongVersion(void) {
#ifdef OPENSSL_VERSION_TEXT
    return SSLeay_version(SSLEAY_VERSION);
#elif defined(OPENSSL_VERSION_NUMBER)
    return folly::format("0x{:x}", OPENSSL_VERSION_NUMBER).str();
#else
    return "";
#endif
  }

  uint64_t getOpenSSLNumericVersion(void) {
#ifdef OPENSSL_VERSION_NUMBER
    return SSLeay();
#else
    return 0;
#endif
  }
};
