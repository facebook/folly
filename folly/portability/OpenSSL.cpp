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
#include <folly/portability/OpenSSL.h>

#include <stdexcept>

namespace folly {
namespace portability {
namespace ssl {

#if OPENSSL_IS_BORINGSSL
int SSL_CTX_set1_sigalgs_list(SSL_CTX*, const char*) {
  return 1; // 0 implies error
}

int TLS1_get_client_version(SSL* s) {
  // Note that this isn't the client version, and the API to
  // get this has been hidden. It may be found by parsing the
  // ClientHello (there is a callback via the SSL_HANDSHAKE struct)
  return s->version;
}
#endif

#if FOLLY_OPENSSL_IS_100
uint32_t SSL_CIPHER_get_id(const SSL_CIPHER* c) {
  return c->id;
}

int TLS1_get_client_version(const SSL* s) {
  return (s->client_version >> 8) == TLS1_VERSION_MAJOR ? s->client_version : 0;
}
#endif

#if FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101
int X509_get_signature_nid(X509* cert) {
  return OBJ_obj2nid(cert->sig_alg->algorithm);
}
#endif

#if FOLLY_OPENSSL_IS_100 || FOLLY_OPENSSL_IS_101 || FOLLY_OPENSSL_IS_102
int SSL_CTX_up_ref(SSL_CTX* ctx) {
  return CRYPTO_add(&ctx->references, 1, CRYPTO_LOCK_SSL_CTX);
}

int SSL_SESSION_up_ref(SSL_SESSION* session) {
  return CRYPTO_add(&session->references, 1, CRYPTO_LOCK_SSL_SESSION);
}

int X509_up_ref(X509* x) {
  return CRYPTO_add(&x->references, 1, CRYPTO_LOCK_X509);
}
#endif

#if !FOLLY_OPENSSL_IS_110
void BIO_meth_free(BIO_METHOD* biom) {
  OPENSSL_free((void*)biom);
}

int BIO_meth_set_read(BIO_METHOD* biom, int (*read)(BIO*, char*, int)) {
  biom->bread = read;
  return 1;
}

int BIO_meth_set_write(BIO_METHOD* biom, int (*write)(BIO*, const char*, int)) {
  biom->bwrite = write;
  return 1;
}

const char* SSL_SESSION_get0_hostname(const SSL_SESSION* s) {
  return s->tlsext_hostname;
}

unsigned char* ASN1_STRING_get0_data(const ASN1_STRING* x) {
  return ASN1_STRING_data((ASN1_STRING*)x);
}

EVP_MD_CTX* EVP_MD_CTX_new(void) {
  EVP_MD_CTX* ctx = (EVP_MD_CTX*)OPENSSL_malloc(sizeof(EVP_MD_CTX));
  if (!ctx) {
    throw std::runtime_error("Cannot allocate EVP_MD_CTX");
  }
  EVP_MD_CTX_init(ctx);
  return ctx;
}

void EVP_MD_CTX_free(EVP_MD_CTX* ctx) {
  EVP_MD_CTX_destroy(ctx);
}

HMAC_CTX* HMAC_CTX_new() {
  HMAC_CTX* ctx = (HMAC_CTX*)OPENSSL_malloc(sizeof(HMAC_CTX));
  if (!ctx) {
    throw std::runtime_error("Cannot allocate HMAC_CTX");
  }
  HMAC_CTX_init(ctx);
  return ctx;
}

void HMAC_CTX_free(HMAC_CTX* ctx) {
  if (ctx) {
    HMAC_CTX_cleanup(ctx);
    OPENSSL_free(ctx);
  }
}

int SSL_SESSION_has_ticket(const SSL_SESSION* s) {
  return (s->tlsext_ticklen > 0) ? 1 : 0;
}

unsigned long SSL_SESSION_get_ticket_lifetime_hint(const SSL_SESSION* s) {
  return s->tlsext_tick_lifetime_hint;
}

// This is taken from OpenSSL 1.1.0
int DH_set0_pqg(DH* dh, BIGNUM* p, BIGNUM* q, BIGNUM* g) {
  /* If the fields p and g in d are NULL, the corresponding input
   * parameters MUST be non-NULL.  q may remain NULL.
   */
  if (dh == nullptr || (dh->p == nullptr && p == nullptr) ||
      (dh->g == nullptr && g == nullptr)) {
    return 0;
  }

  if (p != nullptr) {
    BN_free(dh->p);
    dh->p = p;
  }
  if (q != nullptr) {
    BN_free(dh->q);
    dh->q = q;
  }
  if (g != nullptr) {
    BN_free(dh->g);
    dh->g = g;
  }

  // In OpenSSL 1.1.0, DH_set0_pqg also sets
  //   dh->length = BN_num_bits(q)
  // With OpenSSL 1.0.2, the output of openssl dhparam -C 2048 doesn't set
  // the length field. So as far as the compat lib is concerned, this wrapper
  // mimics the functionality of OpenSSL 1.0.2
  // Note: BoringSSL doesn't even have a length field anymore, just something
  // called 'priv_length'. Let's not mess with that for now.

  return 1;
}
#endif

}
}
}
