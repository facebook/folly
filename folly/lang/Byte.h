/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <cstddef>
#include <type_traits>

//  include or backport:
//  * std::byte
//  * std::to_integer

#if __cpp_lib_byte >= 201603

namespace folly {

using std::byte;
using std::to_integer;

} // namespace folly

#else

namespace folly {

enum class byte : unsigned char {};

constexpr byte operator~(byte b) noexcept {
  return static_cast<byte>(~static_cast<unsigned>(b));
}
constexpr byte operator&(byte b0, byte b1) noexcept {
  return static_cast<byte>(
      static_cast<unsigned>(b0) & static_cast<unsigned>(b1));
}
constexpr byte operator|(byte b0, byte b1) noexcept {
  return static_cast<byte>(
      static_cast<unsigned>(b0) | static_cast<unsigned>(b1));
}
constexpr byte operator^(byte b0, byte b1) noexcept {
  return static_cast<byte>(
      static_cast<unsigned>(b0) ^ static_cast<unsigned>(b1));
}
template <typename I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
constexpr byte operator<<(byte b, I shift) noexcept {
  return static_cast<byte>(static_cast<unsigned>(b) << shift);
}
template <typename I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
constexpr byte operator>>(byte b, I shift) noexcept {
  return static_cast<byte>(static_cast<unsigned>(b) >> shift);
}

constexpr byte& operator&=(byte& b, byte o) noexcept {
  return b = b & o;
}
constexpr byte& operator|=(byte& b, byte o) noexcept {
  return b = b | o;
}
constexpr byte& operator^=(byte& b, byte o) noexcept {
  return b = b ^ o;
}
template <typename I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
constexpr byte& operator<<=(byte& b, I shift) noexcept {
  return b = b << shift;
}
template <typename I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
constexpr byte& operator>>=(byte& b, I shift) noexcept {
  return b = b >> shift;
}

template <typename I, std::enable_if_t<std::is_integral<I>::value, int> = 0>
constexpr I to_integer(byte b) noexcept {
  return static_cast<I>(b);
}

} // namespace folly

#endif
