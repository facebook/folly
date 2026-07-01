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

/**
 * Fuzz target for folly::IPAddress, folly::IPAddressV4, and
 * folly::IPAddressV6.
 *
 * Exercises:
 *  - folly::IPAddress::tryFromString() – non-throwing constructor
 *  - All classification predicates: isV4, isV6, isLoopback, isPrivate,
 *    isMulticast, isLinkLocal, isNonroutable, isZero
 *  - Serialization: str(), toFullyQualified()
 *  - CIDR network parsing via tryCreateNetwork()
 *
 * Security relevance: Malformed IP strings can trigger integer overflows
 * or out-of-bounds reads in address-family detection, CIDR prefix parsing,
 * and IPv4-in-IPv6 embedding logic.  UBSan is especially effective here.
 *
 * OSS-Fuzz integration: https://github.com/google/oss-fuzz/tree/master/projects/folly
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <folly/IPAddress.h>
#include <folly/Range.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 4096) {
    return 0;
  }

  const folly::StringPiece input(
      reinterpret_cast<const char*>(data), size);

  // --- Plain address parsing (non-throwing) ---
  auto addrResult = folly::IPAddress::tryFromString(input);
  if (addrResult.hasValue()) {
    const auto& addr = addrResult.value();
    (void)addr.str();
    (void)addr.toFullyQualified();
    (void)addr.isV4();
    (void)addr.isV6();
    (void)addr.isLoopback();
    (void)addr.isPrivate();
    (void)addr.isMulticast();
    (void)addr.isLinkLocal();
    (void)addr.isNonroutable();
    (void)addr.isZero();
  }

  // --- CIDR network parsing (non-throwing) e.g. "192.168.0.0/24" ---
  try {
    auto netResult = folly::IPAddress::tryCreateNetwork(
        input, /*defaultCidr=*/-1, /*mask=*/false);
    if (netResult.hasValue()) {
      const auto& network = netResult.value();
      (void)network.first.str();
      (void)network.second;
      (void)folly::IPAddress::networkToString(network);
    }
  } catch (...) {
    abort();
  }

  return 0;
}
