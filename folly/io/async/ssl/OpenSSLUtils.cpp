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
#include <folly/io/async/ssl/OpenSSLUtils.h>
#include <folly/ScopeGuard.h>
#include <folly/portability/Sockets.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <glog/logging.h>

namespace folly {
namespace ssl {

bool OpenSSLUtils::getPeerAddressFromX509StoreCtx(X509_STORE_CTX* ctx,
                                                  sockaddr_storage* addrStorage,
                                                  socklen_t* addrLen) {
  // Grab the ssl idx and then the ssl object so that we can get the peer
  // name to compare against the ips in the subjectAltName
  auto sslIdx = SSL_get_ex_data_X509_STORE_CTX_idx();
  auto ssl = reinterpret_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, sslIdx));
  int fd = SSL_get_fd(ssl);
  if (fd < 0) {
    LOG(ERROR) << "Inexplicably couldn't get fd from SSL";
    return false;
  }

  *addrLen = sizeof(*addrStorage);
  if (getpeername(fd, reinterpret_cast<sockaddr*>(addrStorage), addrLen) != 0) {
    PLOG(ERROR) << "Unable to get peer name";
    return false;
  }
  CHECK(*addrLen <= sizeof(*addrStorage));
  return true;
}

bool OpenSSLUtils::validatePeerCertNames(X509* cert,
                                         const sockaddr* addr,
                                         socklen_t /* addrLen */) {
  // Try to extract the names within the SAN extension from the certificate
  auto altNames = reinterpret_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
  SCOPE_EXIT {
    if (altNames != nullptr) {
      sk_GENERAL_NAME_pop_free(altNames, GENERAL_NAME_free);
    }
  };
  if (altNames == nullptr) {
    LOG(WARNING) << "No subjectAltName provided and we only support ip auth";
    return false;
  }

  const sockaddr_in* addr4 = nullptr;
  const sockaddr_in6* addr6 = nullptr;
  if (addr != nullptr) {
    if (addr->sa_family == AF_INET) {
      addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    } else if (addr->sa_family == AF_INET6) {
      addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    } else {
      LOG(FATAL) << "Unsupported sockaddr family: " << addr->sa_family;
    }
  }

  for (int i = 0; i < sk_GENERAL_NAME_num(altNames); i++) {
    auto name = sk_GENERAL_NAME_value(altNames, i);
    if ((addr4 != nullptr || addr6 != nullptr) && name->type == GEN_IPADD) {
      // Extra const-ness for paranoia
      unsigned char const* const rawIpStr = name->d.iPAddress->data;
      int const rawIpLen = name->d.iPAddress->length;

      if (rawIpLen == 4 && addr4 != nullptr) {
        if (::memcmp(rawIpStr, &addr4->sin_addr, rawIpLen) == 0) {
          return true;
        }
      } else if (rawIpLen == 16 && addr6 != nullptr) {
        if (::memcmp(rawIpStr, &addr6->sin6_addr, rawIpLen) == 0) {
          return true;
        }
      } else if (rawIpLen != 4 && rawIpLen != 16) {
        LOG(WARNING) << "Unexpected IP length: " << rawIpLen;
      }
    }
  }

  LOG(WARNING) << "Unable to match client cert against alt name ip";
  return false;
}

} // ssl
} // folly
