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

#include <string>
#include <folly/io/async/SSLContext.h>
#include <vector>

/**
 * SSLContextConfig helps to describe the configs/options for
 * a SSL_CTX. For example:
 *
 *   1. Filename of X509, private key and its password.
 *   2. ciphers list
 *   3. NPN list
 *   4. Is session cache enabled?
 *   5. Is it the default X509 in SNI operation?
 *   6. .... and a few more
 */
namespace folly {

struct SSLContextConfig {
  SSLContextConfig() {}
  ~SSLContextConfig() {}

  struct CertificateInfo {
    std::string certPath;
    std::string keyPath;
    std::string passwordPath;
  };

  /**
   * Helpers to set/add a certificate
   */
  void setCertificate(const std::string& certPath,
                      const std::string& keyPath,
                      const std::string& passwordPath) {
    certificates.clear();
    addCertificate(certPath, keyPath, passwordPath);
  }

  void addCertificate(const std::string& certPath,
                      const std::string& keyPath,
                      const std::string& passwordPath) {
    certificates.emplace_back(CertificateInfo{certPath, keyPath, passwordPath});
  }

  /**
   * Set the optional list of protocols to advertise via TLS
   * Next Protocol Negotiation. An empty list means NPN is not enabled.
   */
  void setNextProtocols(const std::list<std::string>& inNextProtocols) {
    nextProtocols.clear();
    nextProtocols.push_back({1, inNextProtocols});
  }

  typedef std::function<bool(char const* server_name)> SNINoMatchFn;

  std::vector<CertificateInfo> certificates;
  folly::SSLContext::SSLVersion sslVersion{
    folly::SSLContext::TLSv1};
  bool sessionCacheEnabled{true};
  bool sessionTicketEnabled{true};
  bool clientHelloParsingEnabled{false};
  std::string sslCiphers{
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:"
    "AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA:AES256-SHA:"
    "ECDHE-ECDSA-RC4-SHA:ECDHE-RSA-RC4-SHA:RC4-SHA:RC4-MD5:"
    "ECDHE-RSA-DES-CBC3-SHA:DES-CBC3-SHA"};
  std::string eccCurveName;
  // Ciphers to negotiate if TLS version >= 1.1
  std::string tls11Ciphers{""};
  // Weighted lists of NPN strings to advertise
  std::list<folly::SSLContext::NextProtocolsItem>
      nextProtocols;
  bool isLocalPrivateKey{true};
  // Should this SSLContextConfig be the default for SNI purposes
  bool isDefault{false};
  // Callback function to invoke when there are no matching certificates
  // (will only be invoked once)
  SNINoMatchFn sniNoMatchFn;
  // File containing trusted CA's to validate client certificates
  std::string clientCAFile;
};

}
