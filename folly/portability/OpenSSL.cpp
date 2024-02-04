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

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/detail/OpenSSLThreading.h>

#include <stdexcept>

namespace folly {
namespace portability {
namespace ssl {

#ifdef OPENSSL_IS_BORINGSSL
// int SSL_CTX_set1_sigalgs_list(SSL_CTX*, const char*) {
//   return 1; // 0 implies error
// }


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

void X509_STORE_CTX_set0_verified_chain(
    X509_STORE_CTX* ctx, STACK_OF(X509) * sk) {
  sk_X509_pop_free(ctx->chain, X509_free);
  ctx->chain = sk;
}

int X509_STORE_up_ref(X509_STORE* v) {
  return CRYPTO_add(&v->references, 1, CRYPTO_LOCK_X509_STORE);
}

int EVP_PKEY_up_ref(EVP_PKEY* evp) {
  return CRYPTO_add(&evp->references, 1, CRYPTO_LOCK_EVP_PKEY);
}

void RSA_get0_key(
    const RSA* r, const BIGNUM** n, const BIGNUM** e, const BIGNUM** d) {
  if (n != nullptr) {
    *n = r->n;
  }
  if (e != nullptr) {
    *e = r->e;
  }
  if (d != nullptr) {
    *d = r->d;
  }
}

RSA* EVP_PKEY_get0_RSA(EVP_PKEY* pkey) {
  if (pkey->type != EVP_PKEY_RSA) {
    return nullptr;
  }
  return pkey->pkey.rsa;
}

DSA* EVP_PKEY_get0_DSA(EVP_PKEY* pkey) {
  if (pkey->type != EVP_PKEY_DSA) {
    return nullptr;
  }
  return pkey->pkey.dsa;
}

DH* EVP_PKEY_get0_DH(EVP_PKEY* pkey) {
  if (pkey->type != EVP_PKEY_DH) {
    return nullptr;
  }
  return pkey->pkey.dh;
}

EC_KEY* EVP_PKEY_get0_EC_KEY(EVP_PKEY* pkey) {
  if (pkey->type != EVP_PKEY_EC) {
    return nullptr;
  }
  return pkey->pkey.ec;
}
#endif

#if !FOLLY_OPENSSL_IS_110
/* BIO_METHOD* BIO_meth_new(int type, const char* name) {
  BIO_METHOD* method = (BIO_METHOD*)OPENSSL_malloc(sizeof(BIO_METHOD));
  if (method == nullptr) {
    return nullptr;
  }
  memset(method, 0, sizeof(BIO_METHOD));
  method->type = type;
  method->name = name;
  return method;
}

void BIO_meth_free(BIO_METHOD* biom) {
  OPENSSL_free((void*)biom);
} */

const char* SSL_SESSION_get0_hostname(const SSL_SESSION* s) {
  return nullptr;
}

int BIO_meth_set_write(BIO_METHOD* biom, int (*write)(BIO*, const char*, int)) {
  biom->bwrite = write;
  return 1;
}

int BIO_meth_set_puts(BIO_METHOD* biom, int (*bputs)(BIO*, const char*)) {
  biom->bputs = bputs;
  return 1;
}

int BIO_meth_set_gets(BIO_METHOD* biom, int (*bgets)(BIO*, char*, int)) {
  biom->bgets = bgets;
  return 1;
}

int BIO_meth_set_ctrl(BIO_METHOD* biom, long (*ctrl)(BIO*, int, long, void*)) {
  biom->ctrl = ctrl;
  return 1;
}

int BIO_meth_set_create(BIO_METHOD* biom, int (*create)(BIO*)) {
  biom->create = create;
  return 1;
}

int BIO_meth_set_destroy(BIO_METHOD* biom, int (*destroy)(BIO*)) {
  biom->destroy = destroy;
  return 1;
}

void BIO_set_data(BIO* bio, void* ptr) {
  bio->ptr = ptr;
}

void* BIO_get_data(BIO* bio) {
  return bio->ptr;
}

void BIO_set_init(BIO* bio, int init) {
  bio->init = init;
}

void BIO_set_shutdown(BIO* bio, int shutdown) {
  bio->shutdown = shutdown;
}


// unsigned char* ASN1_STRING_get0_data(const ASN1_STRING* x) {
//   return ASN1_STRING_data((ASN1_STRING*)x);
// }



/*
// This is taken from OpenSSL 1.1.0
int DH_set0_pqg(DH* dh, BIGNUM* p, BIGNUM* q, BIGNUM* g) {

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
*/

/* EVP_MD_CTX* EVP_MD_CTX_new() {
  EVP_MD_CTX* ctx = (EVP_MD_CTX*)OPENSSL_malloc(sizeof(EVP_MD_CTX));
  if (!ctx) {
    throw std::runtime_error("Cannot allocate EVP_MD_CTX");
  }
  EVP_MD_CTX_init(ctx);
  return ctx;
}

void EVP_MD_CTX_free(EVP_MD_CTX* ctx) {
  if (ctx) {
    EVP_MD_CTX_cleanup(ctx);
    OPENSSL_free(ctx);
  }
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
*/

int ECDSA_SIG_set0(ECDSA_SIG* sig, BIGNUM* r, BIGNUM* s) {
  // Based off of https://wiki.openssl.org/index.php/OpenSSL_1.1.0_Changes
  if (r == nullptr || s == nullptr) {
    return 0;
  }
  BN_clear_free(sig->r);
  BN_clear_free(sig->s);
  sig->r = r;
  sig->s = s;
  return 1;
}

void ECDSA_SIG_get0(
    const ECDSA_SIG* sig, const BIGNUM** pr, const BIGNUM** ps) {
  // Based off of https://wiki.openssl.org/index.php/OpenSSL_1.1.0_Changes
  if (pr != nullptr) {
    *pr = sig->r;
  }
  if (ps != nullptr) {
    *ps = sig->s;
  }
}

/**
 * Compatibility shim for OpenSSL < 1.1.0.
 *
 * For now, options and settings are ignored. We implement the most common
 * behavior, which is to add all digests, ciphers, and strings.
 */
int OPENSSL_init_ssl(uint64_t, const OPENSSL_INIT_SETTINGS*) {
  // OpenSSL >= 1.1.0 handles initializing the library, adding digests &
  // ciphers, loading strings. Additionally, OpenSSL >= 1.1.0 uses platform
  // native threading & mutexes, which means that we should handle setting up
  // the necessary threading initialization in the compat layer as well.
  SSL_library_init();
  OpenSSL_add_all_ciphers();
  OpenSSL_add_all_digests();
  OpenSSL_add_all_algorithms();

  SSL_load_error_strings();
  ERR_load_crypto_strings();

  // The caller should have used SSLContext::setLockTypes() prior to calling
  // this function.
  folly::ssl::detail::installThreadingLocks();
  return 1;
}

/*
void OPENSSL_cleanup() {
  folly::ssl::detail::cleanupThreadingLocks();
  CRYPTO_cleanup_all_ex_data();
  ERR_free_strings();
  EVP_cleanup();
  ERR_clear_error();
}

const ASN1_INTEGER* X509_REVOKED_get0_serialNumber(const X509_REVOKED* r) {
  return r->serialNumber;
}

const ASN1_TIME* X509_REVOKED_get0_revocationDate(const X509_REVOKED* r) {
  return r->revocationDate;
}
*/

/*
uint32_t X509_get_extension_flags(X509* x) {
  // Tells OpenSSL to load flags
  X509_check_purpose(x, -1, -1);
  return x->ex_flags;
}

uint32_t X509_get_key_usage(X509* x) {
  // Call get_extension_flags rather than accessing directly to force loading
  // of flags
  if ((X509_get_extension_flags(x) & EXFLAG_KUSAGE) == EXFLAG_KUSAGE) {
    return x->ex_kusage;
  }
  return UINT32_MAX;
}

uint32_t X509_get_extended_key_usage(X509* x) {
  return x->ex_xkusage;
}
*/

// int X509_OBJECT_get_type(const X509_OBJECT* obj) {
//  return obj->type;
// }

// X509* X509_OBJECT_get0_X509(const X509_OBJECT* obj) {
//  if (obj == nullptr || obj->type != X509_LU_X509) {
//    return nullptr;
//  }
//  return obj->data.x509;
// }

// const ASN1_TIME* X509_CRL_get0_lastUpdate(const X509_CRL* crl) {
//   return X509_CRL_get_lastUpdate(crl);
// }

// const ASN1_TIME* X509_CRL_get0_nextUpdate(const X509_CRL* crl) {
//   return X509_CRL_get_nextUpdate(crl);
// }

// const X509_ALGOR* X509_get0_tbs_sigalg(const X509* x) {
//   return x->cert_info->signature;
// }

#endif // !FOLLY_OPENSSL_IS_110
} // namespace ssl
} // namespace portability
} // namespace folly
