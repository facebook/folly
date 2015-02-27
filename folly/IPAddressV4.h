/*
 * Copyright 2015 Facebook, Inc.
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

#include <functional>
#include <iostream>

#include <boost/operators.hpp>

#include <folly/Hash.h>
#include <folly/Range.h>
#include <folly/detail/IPAddress.h>

namespace folly {

class IPAddress;
class IPAddressV4;
class IPAddressV6;

/**
 * Pair of IPAddressV4, netmask
 */
typedef std::pair<IPAddressV4, uint8_t> CIDRNetworkV4;

/**
 * Specialization for IPv4 addresses
 */
typedef std::array<uint8_t, 4> ByteArray4;

/**
 * IPv4 variation of IPAddress.
 *
 * Added methods: toLong, toLongHBO and createIPv6
 *
 * @note toLong/fromLong deal in network byte order, use toLongHBO/fromLongHBO
 * if working in host byte order.
 *
 * @see IPAddress
 */
class IPAddressV4 : boost::totally_ordered<IPAddressV4> {
 public:
  // create an IPAddressV4 instance from a uint32_t (network byte order)
  static IPAddressV4 fromLong(uint32_t src);
  // same as above but host byte order
  static IPAddressV4 fromLongHBO(uint32_t src);

  /**
   * Create a new IPAddress instance from the provided binary data.
   * @throws IPAddressFormatException if the input length is not 4 bytes.
   */
  static IPAddressV4 fromBinary(ByteRange bytes) {
    IPAddressV4 addr;
    addr.setFromBinary(bytes);
    return addr;
  }

  /**
   * Convert a IPv4 address string to a long in network byte order.
   * @param [in] ip the address to convert
   * @return the long representation of the address
   */
  static uint32_t toLong(StringPiece ip);
  // Same as above, but in host byte order.
  // This is slightly slower than toLong.
  static uint32_t toLongHBO(StringPiece ip);

  /**
   * Default constructor for IPAddressV4.
   *
   * The address value will be 0.0.0.0
   */
  IPAddressV4();

  // Create an IPAddressV4 from a string
  // @throws IPAddressFormatException
  explicit IPAddressV4(StringPiece ip);

  // ByteArray4 constructor
  explicit IPAddressV4(const ByteArray4& src);

  // in_addr constructor
  explicit IPAddressV4(const in_addr src);

  // Return the V6 mapped representation of the address.
  IPAddressV6 createIPv6() const;

  // Return the long (network byte order) representation of the address.
  uint32_t toLong() const {
    return toAddr().s_addr;
  }

  // Return the long (host byte order) representation of the address.
  // This is slightly slower than toLong.
  uint32_t toLongHBO() const {
    return ntohl(toLong());
  }

  /**
   * @see IPAddress#bitCount
   * @returns 32
   */
  static size_t bitCount() { return 32; }

  /**
   * @See IPAddress#toJson
   */
  std::string toJson() const;

  size_t hash() const {
    static const uint32_t seed = AF_INET;
    uint32_t hashed = hash::fnv32_buf(&addr_, 4);
    return hash::hash_combine(seed, hashed);
  }

  // @see IPAddress#inSubnet
  // @throws IPAddressFormatException if string doesn't contain a V4 address
  bool inSubnet(StringPiece cidrNetwork) const;

  // return true if address is in subnet
  bool inSubnet(const IPAddressV4& subnet, uint8_t cidr) const {
    return inSubnetWithMask(subnet, fetchMask(cidr));
  }
  bool inSubnetWithMask(const IPAddressV4& subnet, const ByteArray4 mask) const;

  // @see IPAddress#isLoopback
  bool isLoopback() const;

  // @see IPAddress#isLinkLocal
  bool isLinkLocal() const;

  // @see IPAddress#isNonroutable
  bool isNonroutable() const;

  // @see IPAddress#isPrivate
  bool isPrivate() const;

  // @see IPAddress#isMulticast
  bool isMulticast() const;

  // @see IPAddress#isZero
  bool isZero() const {
    return detail::Bytes::isZero(bytes(), 4);
  }

  bool isLinkLocalBroadcast() const {
    return (INADDR_BROADCAST == toLongHBO());
  }

  // @see IPAddress#mask
  IPAddressV4 mask(size_t numBits) const;

  // @see IPAddress#str
  std::string str() const;

  // return underlying in_addr structure
  in_addr toAddr() const { return addr_.inAddr_; }

  sockaddr_in toSockAddr() const {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(sockaddr_in));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, &addr_.inAddr_, sizeof(in_addr));
    return addr;
  }

  ByteArray4 toByteArray() const {
    ByteArray4 ba{{0}};
    std::memcpy(ba.data(), bytes(), 4);
    return std::move(ba);
  }

  // @see IPAddress#toFullyQualified
  std::string toFullyQualified() const { return str(); }

  // @see IPAddress#version
  size_t version() const { return 4; }

  /**
   * Return the mask associated with the given number of bits.
   * If for instance numBits was 24 (e.g. /24) then the V4 mask returned should
   * be {0xff, 0xff, 0xff, 0x00}.
   * @param [in] numBits bitmask to retrieve
   * @throws abort if numBits == 0 or numBits > bitCount()
   * @return mask associated with numBits
   */
  static const ByteArray4 fetchMask(size_t numBits);

  // Given 2 IPAddressV4,mask pairs extract the longest common IPAddress,
  // mask pair
  static CIDRNetworkV4 longestCommonPrefix(
    const CIDRNetworkV4& one, const CIDRNetworkV4& two) {
    auto prefix =
      detail::Bytes::longestCommonPrefix(one.first.addr_.bytes_, one.second,
                                         two.first.addr_.bytes_, two.second);
    return {IPAddressV4(prefix.first), prefix.second};
  }
  // Number of bytes in the address representation.
  static size_t byteCount() { return 4; }
  //get nth most significant bit - 0 indexed
  bool getNthMSBit(size_t bitIndex) const {
    return detail::getNthMSBitImpl(*this, bitIndex, AF_INET);
  }
  //get nth most significant byte - 0 indexed
  uint8_t getNthMSByte(size_t byteIndex) const;
  //get nth bit - 0 indexed
  bool getNthLSBit(size_t bitIndex) const {
    return getNthMSBit(bitCount() - bitIndex - 1);
  }
  //get nth byte - 0 indexed
  uint8_t getNthLSByte(size_t byteIndex) const {
    return getNthMSByte(byteCount() - byteIndex - 1);
  }

  const unsigned char* bytes() const { return addr_.bytes_.data(); }

 private:
  union AddressStorage {
    static_assert(sizeof(in_addr) == sizeof(ByteArray4),
                  "size of in_addr and ByteArray4 are different");
    in_addr inAddr_;
    ByteArray4 bytes_;
    AddressStorage() {
      std::memset(this, 0, sizeof(AddressStorage));
    }
    explicit AddressStorage(const ByteArray4 bytes): bytes_(bytes) {}
    explicit AddressStorage(const in_addr addr): inAddr_(addr) {}
  } addr_;

  static const std::array<ByteArray4, 33> masks_;

  /**
   * Set the current IPAddressV4 object to have the address specified by bytes.
   * @throws IPAddressFormatException if bytes.size() is not 4.
   */
  void setFromBinary(ByteRange bytes);
};

// boost::hash uses hash_value() so this allows boost::hash to work
// automatically for IPAddressV4
size_t hash_value(const IPAddressV4& addr);
std::ostream& operator<<(std::ostream& os, const IPAddressV4& addr);
// Define toAppend() to allow IPAddressV4 to be used with to<string>
void toAppend(IPAddressV4 addr, std::string* result);
void toAppend(IPAddressV4 addr, fbstring* result);

/**
 * Return true if two addresses are equal.
 */
inline bool operator==(const IPAddressV4& addr1, const IPAddressV4& addr2) {
  return (addr1.toLong() == addr2.toLong());
}
// Return true if addr1 < addr2
inline bool operator<(const IPAddressV4& addr1, const IPAddressV4& addr2) {
  return (addr1.toLongHBO() < addr2.toLongHBO());
}

}  // folly

namespace std {
template<>
struct hash<folly::IPAddressV4> {
  size_t operator()(const folly::IPAddressV4 addr) const {
    return addr.hash();
  }
};
}  // std
