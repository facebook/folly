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

#pragma once

#include <exception>
#include <string>

#include <folly/Conv.h>
#include <folly/detail/IPAddress.h>

namespace folly {

/**
 * Exception for invalid IP addresses.
 */
class IPAddressFormatException : public std::exception {
 public:
  explicit IPAddressFormatException(const std::string& msg)
      : msg_(msg) {}
  IPAddressFormatException(
    const IPAddressFormatException&) = default;
  template<typename... Args>
  explicit IPAddressFormatException(Args&&... args)
      : msg_(to<std::string>(std::forward<Args>(args)...)) {}

  virtual ~IPAddressFormatException() noexcept {}
  virtual const char *what(void) const noexcept {
    return msg_.c_str();
  }

 private:
  const std::string msg_;
};

class InvalidAddressFamilyException : public IPAddressFormatException {
 public:
  explicit InvalidAddressFamilyException(const std::string& msg)
      : IPAddressFormatException(msg) {}
  InvalidAddressFamilyException(
    const InvalidAddressFamilyException&) = default;
  explicit InvalidAddressFamilyException(sa_family_t family)
      : IPAddressFormatException("Address family " +
                                 detail::familyNameStr(family) +
                                 " is not AF_INET or AF_INET6") {}
  template<typename... Args>
  explicit InvalidAddressFamilyException(Args&&... args)
      : IPAddressFormatException(std::forward<Args>(args)...) {}
};

}  // folly
