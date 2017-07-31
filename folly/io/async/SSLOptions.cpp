/*
 * Copyright 2004-present Facebook, Inc.
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

#include "SSLOptions.h"

namespace folly {
namespace ssl {

const std::vector<std::string>& SSLCommonOptions::getCipherList() {
  static const std::vector<std::string> kCommonCipherList = {
      "ECDHE-ECDSA-AES128-GCM-SHA256",
      "ECDHE-RSA-AES128-GCM-SHA256",
      "ECDHE-ECDSA-AES256-GCM-SHA384",
      "ECDHE-RSA-AES256-GCM-SHA384",
      "ECDHE-ECDSA-AES256-SHA",
      "ECDHE-RSA-AES256-SHA",
      "ECDHE-ECDSA-AES128-SHA",
      "ECDHE-RSA-AES128-SHA",
      "ECDHE-RSA-AES256-SHA384",
      "AES128-GCM-SHA256",
      "AES256-SHA",
      "AES128-SHA",
  };
  return kCommonCipherList;
}

const std::vector<std::string>& SSLCommonOptions::getSignatureAlgorithms() {
  static const std::vector<std::string> kCommonSigAlgs = {
      "RSA+SHA512",
      "ECDSA+SHA512",
      "RSA+SHA384",
      "ECDSA+SHA384",
      "RSA+SHA256",
      "ECDSA+SHA256",
      "RSA+SHA1",
      "ECDSA+SHA1",
  };
  return kCommonSigAlgs;
}

void SSLCommonOptions::setClientOptions(SSLContext& ctx) {
#ifdef SSL_MODE_HANDSHAKE_CUTTHROUGH
  ctx.enableFalseStart();
#endif

  X509VerifyParam param(X509_VERIFY_PARAM_new());
  X509_VERIFY_PARAM_set_flags(param.get(), X509_V_FLAG_X509_STRICT);
  try {
    ctx.setX509VerifyParam(param);
  } catch (std::runtime_error const& e) {
    LOG(DFATAL) << exceptionStr(e);
  }

  try {
    ctx.setClientECCurvesList({"P-256", "P-384"});
  } catch (std::runtime_error const& e) {
    LOG(DFATAL) << exceptionStr(e);
  }

  setCipherSuites<SSLCommonOptions>(ctx);
  setSignatureAlgorithms<SSLCommonOptions>(ctx);
}

} // namespace ssl
} // namespace folly
