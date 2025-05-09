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

#include <array>
#include <cstdint>
#include <string_view>

#include <folly/lang/Align.h>
#include <folly/lang/Assume.h>

namespace folly {

/// hex_alphabet_lower
constexpr auto hex_alphabet_lower = std::string_view("0123456789abcdef");

/// hex_alphabet_upper
constexpr auto hex_alphabet_upper = std::string_view("0123456789ABCDEF");

namespace detail {

constexpr std::array<uint8_t, 256> make_hex_alphabet_table() {
  std::array<uint8_t, 256> array{};
  for (auto& v : array) {
    v = ~0;
  }
  for (uint8_t i = 0; i < hex_alphabet_lower.size(); ++i) {
    array[hex_alphabet_lower[i]] = i;
  }
  for (uint8_t i = 0; i < hex_alphabet_upper.size(); ++i) {
    array[hex_alphabet_upper[i]] = i;
  }
  return array;
}

} // namespace detail

/// hex_alphabet_table
alignas(hardware_constructive_interference_size) //
    constexpr auto hex_alphabet_table = detail::make_hex_alphabet_table();

/// hex_decoded_digit_is_valid
constexpr bool hex_decoded_digit_is_valid(uint8_t const v) {
  return !(v & 0x80);
}

/// hex_is_digit_table
constexpr bool hex_is_digit_table(char const h) {
  return int8_t(hex_alphabet_table[uint8_t(h)]) >= 0;
}

/// hex_decode_digit_table
///
/// For characters which are in the lower or upper hex alphabets, returns the
/// decoded value. For other characters, returns a value with the high bit set.
constexpr uint8_t hex_decode_digit_table(char const h) {
  return hex_alphabet_table[uint8_t(h)];
}

/// hex_is_digit_flavor_x86_64
///
/// from: https://github.com/stedonet/chex
constexpr bool hex_is_digit_flavor_x86_64(char const h) {
  uint8_t const uh = uint8_t(h);
  uint8_t const n09 = uh - '0';
  uint8_t const nAF = (uh | 0x20) - 'a';
  return (n09 < 10) || (nAF < 6);
}

/// hex_decode_digit_raw_flavor_x86_64
///
/// from: https://github.com/stedonet/chex
constexpr uint8_t hex_decode_digit_raw_flavor_x86_64(char const h) {
  return (h + (h >> 6) * 9) & 0xf;
}

/// hex_decode_digit_flavor_x86_64
///
/// from: https://github.com/stedonet/chex
constexpr uint8_t hex_decode_digit_flavor_x86_64(char const h) {
  if (!hex_is_digit_flavor_x86_64(h)) {
    return ~0;
  }
  auto const raw = hex_decode_digit_raw_flavor_x86_64(h);
  assume(!(raw & 0xf0));
  return raw;
}

/// hex_is_digit_flavor_aarch64
constexpr bool hex_is_digit_flavor_aarch64(char const h) {
  auto const uh = uint8_t(h);
  auto const cond = uh <= '9';
  auto const base = uint8_t(cond ? uh - '0' : (uh | 0x20) - 'a');
  auto const bound = uint8_t(cond ? 10 : 6);
  return base < bound;
}

/// hex_decode_digit_raw_flavor_aarch64
constexpr uint8_t hex_decode_digit_raw_flavor_aarch64(char const h) {
  auto const uh = uint8_t(h);
  auto const cond = uh <= '9';
  auto const base = uint8_t(cond ? uh - '0' : (uh | 0x20) - 'a');
  auto const offset = uint8_t(cond ? 0 : 10);
  return base + offset;
}

/// hex_decode_digit_flavor_aarch64
constexpr uint8_t hex_decode_digit_flavor_aarch64(char const h) {
  if (!hex_is_digit_flavor_aarch64(h)) {
    return ~0;
  }
  auto const raw = hex_decode_digit_raw_flavor_aarch64(h);
  assume(!(raw & 0xf0));
  return raw;
}

/// hex_is_digit
constexpr bool hex_is_digit(char const h) {
  return kIsArchAArch64 //
      ? hex_is_digit_flavor_aarch64(h)
      : hex_is_digit_flavor_x86_64(h);
}

/// hex_decode_digit_raw
///
/// For characters which are in the lower or upper hex alphabets, returns the
/// decoded value. For other characters, the behavior is unspecified and any
/// value may be returned. This function cannot be used to distinguish between
/// characters in and outside of the alphabets.
constexpr uint8_t hex_decode_digit_raw(char const h) {
  return kIsArchAArch64 //
      ? hex_decode_digit_raw_flavor_aarch64(h)
      : hex_decode_digit_raw_flavor_x86_64(h);
}

/// hex_decode_digit
///
/// For characters which are in the lower or upper hex alphabets, returns the
/// decoded value. For other characters, returns a value with the high bit set.
constexpr uint8_t hex_decode_digit(char const h) {
  return kIsArchAArch64 //
      ? hex_decode_digit_flavor_aarch64(h)
      : hex_decode_digit_flavor_x86_64(h);
}

} // namespace folly
