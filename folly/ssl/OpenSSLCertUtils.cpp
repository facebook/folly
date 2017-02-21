/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/ssl/OpenSSLCertUtils.h>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <folly/ScopeGuard.h>

namespace folly {
namespace ssl {

Optional<std::string> OpenSSLCertUtils::getCommonName(X509& x509) {
  auto subject = X509_get_subject_name(&x509);
  if (!subject) {
    return none;
  }

  auto cnLoc = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
  if (cnLoc < 0) {
    return none;
  }

  auto cnEntry = X509_NAME_get_entry(subject, cnLoc);
  if (!cnEntry) {
    return none;
  }

  auto cnAsn = X509_NAME_ENTRY_get_data(cnEntry);
  if (!cnAsn) {
    return none;
  }

  auto cnData = reinterpret_cast<const char*>(ASN1_STRING_data(cnAsn));
  auto cnLen = ASN1_STRING_length(cnAsn);
  if (!cnData || cnLen <= 0) {
    return none;
  }

  return Optional<std::string>(std::string(cnData, cnLen));
}

std::vector<std::string> OpenSSLCertUtils::getSubjectAltNames(X509& x509) {
  auto names = reinterpret_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(&x509, NID_subject_alt_name, nullptr, nullptr));
  if (!names) {
    return {};
  }
  SCOPE_EXIT {
    sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
  };

  std::vector<std::string> ret;
  auto count = sk_GENERAL_NAME_num(names);
  for (int i = 0; i < count; i++) {
    auto genName = sk_GENERAL_NAME_value(names, i);
    if (!genName || genName->type != GEN_DNS) {
      continue;
    }
    auto nameData =
        reinterpret_cast<const char*>(ASN1_STRING_data(genName->d.dNSName));
    auto nameLen = ASN1_STRING_length(genName->d.dNSName);
    if (!nameData || nameLen <= 0) {
      continue;
    }
    ret.emplace_back(nameData, nameLen);
  }
  return ret;
}
}
}
