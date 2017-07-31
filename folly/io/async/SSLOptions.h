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

#pragma once

#include <folly/Format.h>
#include <folly/io/async/SSLContext.h>

#include <glog/logging.h>

namespace folly {
namespace ssl {

struct SSLCommonOptions {
  /**
   * Return the cipher list recommended for this options configuration.
   */
  static const std::vector<std::string>& getCipherList();

  /**
   * Return the list of signature algorithms recommended for this options
   * configuration.
   */
  static const std::vector<std::string>& getSignatureAlgorithms();

  /**
   * Set common parameters on a client SSL context, for example,
   * ciphers, signature algorithms, verification options, and client EC curves.
   * @param ctx The SSL Context to which to apply the options.
   */
  static void setClientOptions(SSLContext& ctx);
};

template <typename TSSLOptions>
void setCipherSuites(SSLContext& ctx) {
  try {
    ctx.setCipherList(TSSLOptions::getCipherList());
  } catch (std::runtime_error const& e) {
    LOG(DFATAL) << exceptionStr(e);
  }
}

template <typename TSSLOptions>
void setSignatureAlgorithms(SSLContext& ctx) {
  try {
    ctx.setSignatureAlgorithms(TSSLOptions::getSignatureAlgorithms());
  } catch (std::runtime_error const& e) {
    LOG(DFATAL) << exceptionStr(e);
  }
}

} // namespace ssl
} // namespace folly
