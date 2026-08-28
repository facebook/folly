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

#[cxx::bridge(namespace = "facebook::folly_rust::hash")]
mod ffi {
    struct Hash128 {
        first: u64,
        second: u64,
    }

    unsafe extern "C++" {
        include!("folly/rust/hash/hash.h");

        fn hash_128_to_64(upper: u64, lower: u64) -> u64;
        fn commutative_hash_128_to_64(upper: u64, lower: u64) -> u64;
        fn twang_mix64(key: u64) -> u64;
        fn twang_unmix64(key: u64) -> u64;
        fn twang_32from64(key: u64) -> u32;
        fn jenkins_rev_mix32(key: u32) -> u32;
        fn jenkins_rev_unmix32(key: u32) -> u32;
        fn std_compatible_hash(data: &[u8]) -> u64;

        fn murmur_hash64(data: &[u8], seed: u64) -> u64;

        fn fnv32_append_byte_broken(hash: u32, byte: u8) -> u32;
        fn fnv32_buf_broken(data: &[u8], seed: u32) -> u32;
        fn fnv32_broken(data: &[u8], seed: u32) -> u32;
        fn fnv32_append_byte_fixed(hash: u32, byte: u8) -> u32;
        fn fnv32_buf_fixed(data: &[u8], seed: u32) -> u32;
        fn fnv32_fixed(data: &[u8], seed: u32) -> u32;
        fn fnva32_append_byte(hash: u32, byte: u8) -> u32;
        fn fnva32_buf(data: &[u8], seed: u32) -> u32;
        fn fnva32(data: &[u8], seed: u32) -> u32;
        fn fnv64_append_byte_fixed(hash: u64, byte: u8) -> u64;
        fn fnv64_buf_fixed(data: &[u8], seed: u64) -> u64;
        fn fnv64_fixed(data: &[u8], seed: u64) -> u64;
        fn fnv64_append_byte_broken(hash: u64, byte: u8) -> u64;
        fn fnv64_buf_broken(data: &[u8], seed: u64) -> u64;
        fn fnv64_broken(data: &[u8], seed: u64) -> u64;
        fn fnv32_append_byte(hash: u32, byte: u8) -> u32;
        fn fnv32_buf(data: &[u8], seed: u32) -> u32;
        fn fnv32(data: &[u8], seed: u32) -> u32;
        fn fnv64_append_byte(hash: u64, byte: u8) -> u64;
        fn fnv64_buf(data: &[u8], seed: u64) -> u64;
        fn fnv64(data: &[u8], seed: u64) -> u64;
        fn fnva64_append_byte(hash: u64, byte: u8) -> u64;
        fn fnva64_buf(data: &[u8], seed: u64) -> u64;
        fn fnva64(data: &[u8], seed: u64) -> u64;

        fn hsieh_hash32_buf_constexpr(data: &[u8]) -> u32;
        fn hsieh_hash32_buf(data: &[u8]) -> u32;
        fn hsieh_hash32(data: &[u8]) -> u32;
        fn hsieh_hash32_str(data: &[u8]) -> u32;

        fn spooky_hash_v1_hash32(data: &[u8], seed: u32) -> u32;
        fn spooky_hash_v1_hash64(data: &[u8], seed: u64) -> u64;
        fn spooky_hash_v1_hash128(data: &[u8], seed1: u64, seed2: u64) -> Hash128;
        fn spooky_hash_v2_hash32(data: &[u8], seed: u32) -> u32;
        fn spooky_hash_v2_hash64(data: &[u8], seed: u64) -> u64;
        fn spooky_hash_v2_hash128(data: &[u8], seed1: u64, seed2: u64) -> Hash128;
    }
}

pub(crate) use self::ffi::*;
