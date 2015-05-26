/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/ssl/SSLUtil.h>

#include <folly/Memory.h>

#if OPENSSL_VERSION_NUMBER >= 0x1000105fL
#define OPENSSL_GE_101 1
#include <openssl/asn1.h>
#include <openssl/x509v3.h>
#else
#undef OPENSSL_GE_101
#endif

namespace folly {

std::mutex SSLUtil::sIndexLock_;

std::unique_ptr<std::string> SSLUtil::getCommonName(const X509* cert) {
  X509_NAME* subject = X509_get_subject_name((X509*)cert);
  if (!subject) {
    return nullptr;
  }
  char cn[ub_common_name + 1];
  int res = X509_NAME_get_text_by_NID(subject, NID_commonName,
                                      cn, ub_common_name);
  if (res <= 0) {
    return nullptr;
  } else {
    cn[ub_common_name] = '\0';
    return folly::make_unique<std::string>(cn);
  }
}

std::unique_ptr<std::list<std::string>> SSLUtil::getSubjectAltName(
    const X509* cert) {
#ifdef OPENSSL_GE_101
  auto nameList = folly::make_unique<std::list<std::string>>();
  GENERAL_NAMES* names = (GENERAL_NAMES*)X509_get_ext_d2i(
      (X509*)cert, NID_subject_alt_name, nullptr, nullptr);
  if (names) {
    auto guard = folly::makeGuard([names] { GENERAL_NAMES_free(names); });
    size_t count = sk_GENERAL_NAME_num(names);
    CHECK(count < std::numeric_limits<int>::max());
    for (int i = 0; i < (int)count; ++i) {
      GENERAL_NAME* generalName = sk_GENERAL_NAME_value(names, i);
      if (generalName->type == GEN_DNS) {
        ASN1_STRING* s = generalName->d.dNSName;
        const char* name = (const char*)ASN1_STRING_data(s);
        // I can't find any docs on what a negative return value here
        // would mean, so I'm going to ignore it.
        auto len = ASN1_STRING_length(s);
        DCHECK(len >= 0);
        if (size_t(len) != strlen(name)) {
          // Null byte(s) in the name; return an error rather than depending on
          // the caller to safely handle this case.
          return nullptr;
        }
        nameList->emplace_back(name);
      }
    }
  }
  return nameList;
#else
  return nullptr;
#endif
}

}
