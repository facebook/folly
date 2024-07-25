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

#pragma once

#include <cstdint>

// This must come before the OpenSSL includes.
#include <folly/portability/Windows.h>

#include <folly/Portability.h>

#include <openssl/opensslv.h>

// The OpenSSL public header that describes build time configuration and
// availability (or lack of availability) of certain optional ciphers.
#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
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

#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif

#ifndef OPENSSL_NO_OCSP
#include <openssl/ocsp.h>
#endif

#if OPENSSL_VERSION_NUMBER < 0x10101000L
#error openssl < 1.1.1
#endif

// BoringSSL doesn't have notion of versioning although it defines
// OPENSSL_VERSION_NUMBER to maintain compatibility. The following variables are
// intended to be specific to OpenSSL.
#if !defined(OPENSSL_IS_BORINGSSL)
// OPENSSL_VERSION_{MAJOR,MINOR} only introduced in 3.0, so need to
// test if they are defined first
#if defined(OPENSSL_VERSION_MAJOR) && defined(OPENSSL_VERSION_MINOR)
#define FOLLY_OPENSSL_IS_3X OPENSSL_VERSION_MAJOR == 3

#define FOLLY_OPENSSL_IS_30X \
  OPENSSL_VERSION_MAJOR == 3 && OPENSSL_VERSION_MINOR == 0
#else
#define FOLLY_OPENSSL_IS_3X 0
#define FOLLY_OPENSSL_IS_30X 0
#endif

// Defined according to version number description in
// https://www.openssl.org/docs/man1.1.1/man3/OPENSSL_VERSION_NUMBER.html
// ie. (nibbles) MNNFFPPS: major minor fix patch status
#define FOLLY_OPENSSL_CALCULATE_VERSION(major, minor, fix) \
  (((major << 28) | ((minor << 20) | (fix << 12))))
#define FOLLY_OPENSSL_PREREQ(major, minor, fix) \
  (OPENSSL_VERSION_NUMBER >= FOLLY_OPENSSL_CALCULATE_VERSION(major, minor, fix))
#endif

// OpenSSL 1.1.1 introduced several new ciphers and digests. Unless they are
// explicitly compiled out, they are assumed to be present
#if !defined(OPENSSL_NO_BLAKE2)
#define FOLLY_OPENSSL_HAS_BLAKE2B 1
#else
#define FOLLY_OPENSSL_HAS_BLAKE2B 0
#endif

#if !defined(OPENSSL_NO_CHACHA) || !defined(OPENSSL_NO_POLY1305)
#define FOLLY_OPENSSL_HAS_CHACHA 1
#else
#define FOLLY_OPENSSL_HAS_CHACHA 0
#endif
