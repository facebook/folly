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

#include <folly/rust/hash/hash.h>

#include <string>
#include <string_view>

#include <folly/hash/Hash.h>

#include <folly/rust/hash/ffi.rs.h>

namespace facebook::folly_rust::hash {
namespace {

const char* asChars(rust::Slice<const uint8_t> data) {
  return reinterpret_cast<const char*>(data.data());
}

std::string asString(rust::Slice<const uint8_t> data) {
  return std::string(asChars(data), data.size());
}

std::string_view asStringView(rust::Slice<const uint8_t> data) {
  return std::string_view(asChars(data), data.size());
}

} // namespace

uint64_t hash_128_to_64(uint64_t upper, uint64_t lower) {
  return ::folly::hash::hash_128_to_64(upper, lower);
}

uint64_t commutative_hash_128_to_64(uint64_t upper, uint64_t lower) {
  return ::folly::hash::commutative_hash_128_to_64(upper, lower);
}

uint64_t twang_mix64(uint64_t key) {
  return ::folly::hash::twang_mix64(key);
}

uint64_t twang_unmix64(uint64_t key) {
  return ::folly::hash::twang_unmix64(key);
}

uint32_t twang_32from64(uint64_t key) {
  return ::folly::hash::twang_32from64(key);
}

uint32_t jenkins_rev_mix32(uint32_t key) {
  return ::folly::hash::jenkins_rev_mix32(key);
}

uint32_t jenkins_rev_unmix32(uint32_t key) {
  return ::folly::hash::jenkins_rev_unmix32(key);
}

uint64_t std_compatible_hash(rust::Slice<const uint8_t> data) {
  return ::folly::hash::stdCompatibleHash(asStringView(data));
}

uint64_t murmur_hash64(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::murmurHash64(asChars(data), data.size(), seed);
}

uint32_t fnv32_append_byte_broken(uint32_t hash, uint8_t byte) {
  return ::folly::hash::fnv32_append_byte_BROKEN(hash, byte);
}

uint32_t fnv32_buf_broken(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32_buf_BROKEN(data.data(), data.size(), seed);
}

uint32_t fnv32_broken(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32_BROKEN(asString(data), seed);
}

uint32_t fnv32_append_byte_fixed(uint32_t hash, uint8_t byte) {
  return ::folly::hash::fnv32_append_byte_FIXED(hash, byte);
}

uint32_t fnv32_buf_fixed(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32_buf_FIXED(data.data(), data.size(), seed);
}

uint32_t fnv32_fixed(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32_FIXED(asString(data), seed);
}

uint32_t fnva32_append_byte(uint32_t hash, uint8_t byte) {
  return ::folly::hash::fnva32_append_byte(hash, byte);
}

uint32_t fnva32_buf(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnva32_buf(data.data(), data.size(), seed);
}

uint32_t fnva32(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnva32(asString(data), seed);
}

uint64_t fnv64_append_byte_fixed(uint64_t hash, uint8_t byte) {
  return ::folly::hash::fnv64_append_byte_FIXED(hash, byte);
}

uint64_t fnv64_buf_fixed(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64_buf_FIXED(data.data(), data.size(), seed);
}

uint64_t fnv64_fixed(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64_FIXED(asStringView(data), seed);
}

uint64_t fnv64_append_byte_broken(uint64_t hash, uint8_t byte) {
  return ::folly::hash::fnv64_append_byte_BROKEN(hash, byte);
}

uint64_t fnv64_buf_broken(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64_buf_BROKEN(data.data(), data.size(), seed);
}

uint64_t fnv64_broken(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64_BROKEN(asStringView(data), seed);
}

uint32_t fnv32_append_byte(uint32_t hash, uint8_t byte) {
  return ::folly::hash::fnv32_append_byte(hash, byte);
}

uint32_t fnv32_buf(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32_buf(data.data(), data.size(), seed);
}

uint32_t fnv32(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::fnv32(asString(data), seed);
}

uint64_t fnv64_append_byte(uint64_t hash, uint8_t byte) {
  return ::folly::hash::fnv64_append_byte(hash, byte);
}

uint64_t fnv64_buf(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64_buf(data.data(), data.size(), seed);
}

uint64_t fnv64(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnv64(asStringView(data), seed);
}

uint64_t fnva64_append_byte(uint64_t hash, uint8_t byte) {
  return ::folly::hash::fnva64_append_byte(hash, byte);
}

uint64_t fnva64_buf(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnva64_buf(data.data(), data.size(), seed);
}

uint64_t fnva64(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::fnva64(asString(data), seed);
}

uint32_t hsieh_hash32_buf_constexpr(rust::Slice<const uint8_t> data) {
  return ::folly::hash::hsieh_hash32_buf_constexpr(data.data(), data.size());
}

uint32_t hsieh_hash32_buf(rust::Slice<const uint8_t> data) {
  return ::folly::hash::hsieh_hash32_buf(data.data(), data.size());
}

uint32_t hsieh_hash32(rust::Slice<const uint8_t> data) {
  return ::folly::hash::hsieh_hash32(asString(data).c_str());
}

uint32_t hsieh_hash32_str(rust::Slice<const uint8_t> data) {
  return ::folly::hash::hsieh_hash32_str(asString(data));
}

uint32_t spooky_hash_v1_hash32(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::SpookyHashV1::Hash32(data.data(), data.size(), seed);
}

uint64_t spooky_hash_v1_hash64(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::SpookyHashV1::Hash64(data.data(), data.size(), seed);
}

Hash128 spooky_hash_v1_hash128(
    rust::Slice<const uint8_t> data, uint64_t seed1, uint64_t seed2) {
  ::folly::hash::SpookyHashV1::Hash128(
      data.data(), data.size(), &seed1, &seed2);
  return Hash128{seed1, seed2};
}

uint32_t spooky_hash_v2_hash32(rust::Slice<const uint8_t> data, uint32_t seed) {
  return ::folly::hash::SpookyHashV2::Hash32(data.data(), data.size(), seed);
}

uint64_t spooky_hash_v2_hash64(rust::Slice<const uint8_t> data, uint64_t seed) {
  return ::folly::hash::SpookyHashV2::Hash64(data.data(), data.size(), seed);
}

Hash128 spooky_hash_v2_hash128(
    rust::Slice<const uint8_t> data, uint64_t seed1, uint64_t seed2) {
  ::folly::hash::SpookyHashV2::Hash128(
      data.data(), data.size(), &seed1, &seed2);
  return Hash128{seed1, seed2};
}

} // namespace facebook::folly_rust::hash
