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

#include <exception>
#include <string>
#include <utility>

#include <folly/CPortability.h>
#include <folly/detail/IPAddress.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * Error codes for non-throwing interface of IPAddress family of functions.
 */
enum class IPAddressFormatError { INVALID_IP, UNSUPPORTED_ADDR_FAMILY };

/**
 * Wraps error from parsing IP/MASK string
 */
enum class CIDRNetworkError {
  INVALID_DEFAULT_CIDR,
  INVALID_IP_SLASH_CIDR,
  INVALID_IP,
  INVALID_CIDR,
  CIDR_MISMATCH,
};

/**
 * Exception for invalid IP addresses.
 */
class FOLLY_EXPORT IPAddressFormatException : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class FOLLY_EXPORT InvalidAddressFamilyException
    : public IPAddressFormatException {
 public:
  explicit InvalidAddressFamilyException(const char* msg)
      : IPAddressFormatException{msg} {}
  explicit InvalidAddressFamilyException(const std::string& msg) noexcept
      : IPAddressFormatException{msg} {}
  explicit InvalidAddressFamilyException(sa_family_t family) noexcept
      : InvalidAddressFamilyException(
            "Address family " + detail::familyNameStr(family) +
            " is not AF_INET or AF_INET6") {}
};

} // namespace folly
