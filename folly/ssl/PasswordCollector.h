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

#include <ostream>
#include <string>

namespace folly {
namespace ssl {
/**
 * Overrides the default password collector.
 */
class PasswordCollector {
 public:
  virtual ~PasswordCollector() = default;
  /**
   * Interface for customizing how to collect private key password.
   *
   * By default, OpenSSL prints a prompt on screen and request for password
   * while loading private key. To implement a custom password collector,
   * implement this interface and register it with SSLContext.
   *
   * @param password Pass collected password back to OpenSSL
   * @param size     Maximum length of password including nullptr character
   */
  virtual void getPassword(std::string& password, int size) const = 0;

  /**
   * Returns a description of this collector for logging purposes
   */
  virtual const std::string& describe() const = 0;
};

std::ostream& operator<<(
    std::ostream& os, const folly::ssl::PasswordCollector& collector);

} // namespace ssl
} // namespace folly
