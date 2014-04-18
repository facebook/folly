/*
 * Copyright 2014 Facebook, Inc.
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

#include <boost/noncopyable.hpp>
#include <glog/logging.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <sstream>
#include <type_traits>
#include <vector>

extern "C" {
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
}

#include "folly/Conv.h"
#include "folly/Format.h"

namespace folly { namespace detail {

inline std::string familyNameStr(sa_family_t family) {
  switch (family) {
    case AF_INET:
      return "AF_INET";
    case AF_INET6:
      return "AF_INET6";
    case AF_UNSPEC:
      return "AF_UNSPEC";
    case AF_UNIX:
      return "AF_UNIX";
    default:
      return folly::format("sa_family_t({})",
          folly::to<std::string>(family)).str();
  }
}

template<typename IPAddrType>
inline bool getNthMSBitImpl(const IPAddrType& ip, uint8_t bitIndex,
    sa_family_t family) {
  if (bitIndex >= ip.bitCount()) {
    throw std::invalid_argument(folly::to<std::string>("Bit index must be < ",
          ip.bitCount(), " for addresses of type :", familyNameStr(family)));
  }
  //Underlying bytes are in n/w byte order
  return (ip.getNthMSByte(bitIndex / 8) & (0x80 >> (bitIndex % 8))) != 0;
}

/**
 * Helper for working with unsigned char* or uint8_t* ByteArray values
 */
struct Bytes : private boost::noncopyable {
  // return true if all values of src are zero
  static bool isZero(const uint8_t* src, std::size_t len) {
    for (auto i = 0; i < len; i++) {
      if (src[i] != 0x00) {
        return false;
      }
    }
    return true;
  }

  // mask the values from two byte arrays, returning a new byte array
  template<std::size_t N>
  static std::array<uint8_t, N> mask(const std::array<uint8_t, N>& a,
                                     const std::array<uint8_t, N>& b) {
    static_assert(N > 0, "Can't mask an empty ByteArray");
    std::size_t asize = a.size();
    std::array<uint8_t, N> ba{{0}};
    for (int i = 0; i < asize; i++) {
      ba[i] = a[i] & b[i];
    }
    return ba;
  }

  template<std::size_t N>
  static std::pair<std::array<uint8_t, N>, uint8_t>
  longestCommonPrefix(
    const std::array<uint8_t, N>& one, uint8_t oneMask,
    const std::array<uint8_t, N>& two, uint8_t twoMask) {
    static constexpr auto kBitCount = N * 8;
    static constexpr std::array<uint8_t, 8> kMasks {{
      0x80, // /1
      0xc0, // /2
      0xe0, // /3
      0xf0, // /4
      0xf8, // /5
      0xfc, // /6
      0xfe, // /7
      0xff  // /8
    }};
    if (oneMask > kBitCount || twoMask > kBitCount) {
      throw std::invalid_argument(folly::to<std::string>("Invalid mask "
            "length: ", oneMask > twoMask ? oneMask : twoMask,
            ". Mask length must be <= ", kBitCount));
    }

    auto mask = std::min(oneMask, twoMask);
    uint8_t byteIndex = 0;
    std::array<uint8_t, N> ba{{0}};
    // Compare a byte at a time. Note - I measured compared this with
    // going multiple bytes at a time (8, 4, 2 and 1). It turns out
    // to be 20 - 25% slower for 4 and 16 byte arrays.
    while (byteIndex * 8 < mask && one[byteIndex] == two[byteIndex]) {
      ba[byteIndex] = one[byteIndex];
      ++byteIndex;
    }
    auto bitIndex = std::min(mask, (uint8_t)(byteIndex * 8));
    // Compute the bit up to which the two byte arrays match in the
    // unmatched byte.
    // Here the check is bitIndex < mask since the 0th mask entry in
    // kMasks array holds the mask for masking the MSb in this byte.
    // We could instead make it hold so that no 0th entry masks no
    // bits but thats a useless iteration.
    while (bitIndex < mask && ((one[bitIndex / 8] & kMasks[bitIndex % 8]) ==
        (two[bitIndex / 8] & kMasks[bitIndex % 8]))) {
      ba[bitIndex / 8] = one[bitIndex / 8] & kMasks[bitIndex % 8];
      ++bitIndex;
    }
    return {ba, bitIndex};
  }

  // create an in_addr from an uint8_t*
  static inline in_addr mkAddress4(const uint8_t* src) {
    union {
      in_addr addr;
      uint8_t bytes[4];
    } addr;
    std::memset(&addr, 0, 4);
    std::memcpy(addr.bytes, src, 4);
    return addr.addr;
  }

  // create an in6_addr from an uint8_t*
  static inline in6_addr mkAddress6(const uint8_t* src) {
    in6_addr addr;
    std::memset(&addr, 0, 16);
    std::memcpy(addr.s6_addr, src, 16);
    return addr;
  }

  // convert an uint8_t* to its hex value
  static std::string toHex(const uint8_t* src, std::size_t len) {
    static const char* const lut = "0123456789abcdef";
    std::stringstream ss;
    for (int i = 0; i < len; i++) {
      const unsigned char c = src[i];
      ss << lut[c >> 4] << lut[c & 15];
    }
    return ss.str();
  }

 private:
  Bytes() = delete;
  ~Bytes() = delete;
};

}}  // folly::detail
