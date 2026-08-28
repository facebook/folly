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

#include <cstdint>

#include "rust/cxx.h"

namespace facebook::folly_rust::hash {

struct Hash128;

uint64_t hash_128_to_64(uint64_t upper, uint64_t lower);
uint64_t commutative_hash_128_to_64(uint64_t upper, uint64_t lower);
uint64_t twang_mix64(uint64_t key);
uint64_t twang_unmix64(uint64_t key);
uint32_t twang_32from64(uint64_t key);
uint32_t jenkins_rev_mix32(uint32_t key);
uint32_t jenkins_rev_unmix32(uint32_t key);
uint64_t std_compatible_hash(rust::Slice<const uint8_t> data);

uint64_t murmur_hash64(rust::Slice<const uint8_t> data, uint64_t seed);

uint32_t fnv32_append_byte_broken(uint32_t hash, uint8_t byte);
uint32_t fnv32_buf_broken(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnv32_broken(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnv32_append_byte_fixed(uint32_t hash, uint8_t byte);
uint32_t fnv32_buf_fixed(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnv32_fixed(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnva32_append_byte(uint32_t hash, uint8_t byte);
uint32_t fnva32_buf(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnva32(rust::Slice<const uint8_t> data, uint32_t seed);
uint64_t fnv64_append_byte_fixed(uint64_t hash, uint8_t byte);
uint64_t fnv64_buf_fixed(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnv64_fixed(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnv64_append_byte_broken(uint64_t hash, uint8_t byte);
uint64_t fnv64_buf_broken(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnv64_broken(rust::Slice<const uint8_t> data, uint64_t seed);
uint32_t fnv32_append_byte(uint32_t hash, uint8_t byte);
uint32_t fnv32_buf(rust::Slice<const uint8_t> data, uint32_t seed);
uint32_t fnv32(rust::Slice<const uint8_t> data, uint32_t seed);
uint64_t fnv64_append_byte(uint64_t hash, uint8_t byte);
uint64_t fnv64_buf(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnv64(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnva64_append_byte(uint64_t hash, uint8_t byte);
uint64_t fnva64_buf(rust::Slice<const uint8_t> data, uint64_t seed);
uint64_t fnva64(rust::Slice<const uint8_t> data, uint64_t seed);

uint32_t hsieh_hash32_buf_constexpr(rust::Slice<const uint8_t> data);
uint32_t hsieh_hash32_buf(rust::Slice<const uint8_t> data);
uint32_t hsieh_hash32(rust::Slice<const uint8_t> data);
uint32_t hsieh_hash32_str(rust::Slice<const uint8_t> data);

uint32_t spooky_hash_v1_hash32(rust::Slice<const uint8_t> data, uint32_t seed);
uint64_t spooky_hash_v1_hash64(rust::Slice<const uint8_t> data, uint64_t seed);
Hash128 spooky_hash_v1_hash128(
    rust::Slice<const uint8_t> data, uint64_t seed1, uint64_t seed2);
uint32_t spooky_hash_v2_hash32(rust::Slice<const uint8_t> data, uint32_t seed);
uint64_t spooky_hash_v2_hash64(rust::Slice<const uint8_t> data, uint64_t seed);
Hash128 spooky_hash_v2_hash128(
    rust::Slice<const uint8_t> data, uint64_t seed1, uint64_t seed2);

} // namespace facebook::folly_rust::hash
