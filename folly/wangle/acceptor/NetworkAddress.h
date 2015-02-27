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

#include <folly/SocketAddress.h>

namespace folly {

/**
 * A simple wrapper around SocketAddress that represents
 * a network in CIDR notation
 */
class NetworkAddress {
public:
  /**
   * Create a NetworkAddress for an addr/prefixLen
   * @param addr         IPv4 or IPv6 address of the network
   * @param prefixLen    Prefix length, in bits
   */
  NetworkAddress(const folly::SocketAddress& addr,
      unsigned prefixLen):
    addr_(addr), prefixLen_(prefixLen) {}

  /** Get the network address */
  const folly::SocketAddress& getAddress() const {
    return addr_;
  }

  /** Get the prefix length in bits */
  unsigned getPrefixLength() const { return prefixLen_; }

  /** Check whether a given address lies within the network */
  bool contains(const folly::SocketAddress& addr) const {
    return addr_.prefixMatch(addr, prefixLen_);
  }

  /** Comparison operator to enable use in ordered collections */
  bool operator<(const NetworkAddress& other) const {
    if (addr_ <  other.addr_) {
      return true;
    } else if (other.addr_ < addr_) {
      return false;
    } else {
      return (prefixLen_ < other.prefixLen_);
    }
  }

private:
  folly::SocketAddress addr_;
  unsigned prefixLen_;
};

} // namespace
