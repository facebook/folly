/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/String.h>
#include <mutex>
#include <folly/io/async/AsyncSSLSocket.h>

namespace folly {

/**
 * SSL session establish/resume status
 *
 * changing these values will break logging pipelines
 */
enum class SSLResumeEnum : uint8_t {
  HANDSHAKE = 0,
  RESUME_SESSION_ID = 1,
  RESUME_TICKET = 3,
  NA = 2
};

enum class SSLErrorEnum {
  NO_ERROR,
  TIMEOUT,
  DROPPED
};

class SSLUtil {
 private:
  static std::mutex sIndexLock_;

 public:
  /**
   * Ensures only one caller will allocate an ex_data index for a given static
   * or global.
   */
  static void getSSLCtxExIndex(int* pindex) {
    std::lock_guard<std::mutex> g(sIndexLock_);
    if (*pindex < 0) {
      *pindex = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    }
  }

  static void getRSAExIndex(int* pindex) {
    std::lock_guard<std::mutex> g(sIndexLock_);
    if (*pindex < 0) {
      *pindex = RSA_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    }
  }

  static inline std::string hexlify(const std::string& binary) {
    std::string hex;
    folly::hexlify<std::string, std::string>(binary, hex);

    return hex;
  }

  static inline const std::string& hexlify(const std::string& binary,
                                           std::string& hex) {
    folly::hexlify<std::string, std::string>(binary, hex);

    return hex;
  }

  /**
   * Return the SSL resume type for the given socket.
   */
  static inline SSLResumeEnum getResumeState(
    AsyncSSLSocket* sslSocket) {
    return sslSocket->getSSLSessionReused() ?
      (sslSocket->sessionIDResumed() ?
        SSLResumeEnum::RESUME_SESSION_ID :
        SSLResumeEnum::RESUME_TICKET) :
      SSLResumeEnum::HANDSHAKE;
  }

  /**
   * Get the Common Name from an X.509 certificate
   * @param cert  certificate to inspect
   * @return  common name, or null if an error occurs
   */
  static std::unique_ptr<std::string> getCommonName(const X509* cert);

  /**
   * Get the Subject Alternative Name value(s) from an X.509 certificate
   * @param cert  certificate to inspect
   * @return  set of zero or more alternative names, or null if
   *            an error occurs
   */
  static std::unique_ptr<std::list<std::string>> getSubjectAltName(
      const X509* cert);
};

}
