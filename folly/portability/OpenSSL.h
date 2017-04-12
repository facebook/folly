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

#pragma once

// This must come before the OpenSSL includes.
#include <folly/portability/Windows.h>

#include <folly/Portability.h>

#include <openssl/opensslv.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#ifndef OPENSSL_NO_EC
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#endif

// BoringSSL doesn't have notion of versioning although it defines
// OPENSSL_VERSION_NUMBER to maintain compatibility. The following variables are
// intended to be specific to OpenSSL.
#if !defined(OPENSSL_IS_BORINGSSL)
# define FOLLY_OPENSSL_IS_100                \
  (OPENSSL_VERSION_NUMBER >= 0x10000003L && \
   OPENSSL_VERSION_NUMBER < 0x1000105fL)
# define FOLLY_OPENSSL_IS_101                \
  (OPENSSL_VERSION_NUMBER >= 0x1000105fL && \
   OPENSSL_VERSION_NUMBER < 0x1000200fL)
# define FOLLY_OPENSSL_IS_102                \
  (OPENSSL_VERSION_NUMBER >= 0x1000200fL && \
   OPENSSL_VERSION_NUMBER < 0x10100000L)
# define FOLLY_OPENSSL_IS_110 (OPENSSL_VERSION_NUMBER >= 0x10100000L)
#endif

#if !OPENSSL_IS_BORINGSSL && !FOLLY_OPENSSL_IS_100 && !FOLLY_OPENSSL_IS_101 \
    && !FOLLY_OPENSSL_IS_102 && !FOLLY_OPENSSL_IS_110
# warning Compiling with unsupported OpenSSL version
#endif

// BoringSSL and OpenSSL 0.9.8f later with TLS extension support SNI.
#if OPENSSL_IS_BORINGSSL ||          \
    (OPENSSL_VERSION_NUMBER >= 0x00908070L && !defined(OPENSSL_NO_TLSEXT))
# define FOLLY_OPENSSL_HAS_SNI 1
#else
# define FOLLY_OPENSSL_HAS_SNI 0
#endif

// BoringSSL and OpenSSL 1.0.2 later with TLS extension support ALPN.
#if OPENSSL_IS_BORINGSSL ||          \
    (OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT))
# define FOLLY_OPENSSL_HAS_ALPN 1
#else
# define FOLLY_OPENSSL_HAS_ALPN 0
#endif

// This attempts to "unify" the OpenSSL libcrypto/libssl APIs between
// OpenSSL 1.0.2, 1.1.0 (and some earlier versions) and BoringSSL. The general
// idea is to provide namespaced wrapper methods for versions which do not
// which already exist in BoringSSL and 1.1.0, but there are few APIs such as
// SSL_CTX_set1_sigalgs_list and so on which exist in 1.0.2 but were removed
// in BoringSSL
namespace folly {
namespace portability {
namespace ssl {

#if OPENSSL_IS_BORINGSSL
int SSL_CTX_set1_sigalgs_list(SSL_CTX* ctx, const char* sigalgs_list);
int TLS1_get_client_version(SSL* s);
#endif

#if FOLLY_OPENSSL_IS_100
uint32_t SSL_CIPHER_get_id(const SSL_CIPHER*);
int TLS1_get_client_version(const SSL*);
#endif

#if FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101
int X509_get_signature_nid(X509* cert);
#endif

#if FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101 || FOLLY_OPENSSL_IS_102
int SSL_CTX_up_ref(SSL_CTX* session);
int SSL_SESSION_up_ref(SSL_SESSION* session);
int X509_up_ref(X509* x);
#endif

#if !FOLLY_OPENSSL_IS_110
void BIO_meth_free(BIO_METHOD* biom);
int BIO_meth_set_read(BIO_METHOD* biom, int (*read)(BIO*, char*, int));
int BIO_meth_set_write(BIO_METHOD* biom, int (*write)(BIO*, const char*, int));

const char* SSL_SESSION_get0_hostname(const SSL_SESSION* s);
unsigned char* ASN1_STRING_get0_data(const ASN1_STRING* x);

EVP_MD_CTX* EVP_MD_CTX_new();
void EVP_MD_CTX_free(EVP_MD_CTX* ctx);

HMAC_CTX* HMAC_CTX_new();
void HMAC_CTX_free(HMAC_CTX* ctx);

unsigned long SSL_SESSION_get_ticket_lifetime_hint(const SSL_SESSION* s);
int SSL_SESSION_has_ticket(const SSL_SESSION* s);
int DH_set0_pqg(DH* dh, BIGNUM* p, BIGNUM* q, BIGNUM* g);
#endif

}
}
}

FOLLY_PUSH_WARNING
#if __CLANG_PREREQ(3, 0)
FOLLY_GCC_DISABLE_WARNING("-Wheader-hygiene")
#endif
/* using override */ using namespace folly::portability::ssl;
FOLLY_POP_WARNING
