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

//! Rust bindings for Folly's concrete hash algorithms.
//!
//! This crate exposes the non-template hashing functions from Folly's hash
//! library through a thin CXX bridge. C++ overloads are collapsed into one
//! Rust function per distinct Folly name, byte and string inputs are accepted
//! as borrowed byte slices, and seeded algorithms always require an explicit
//! seed. The four standard FNV offset bases are available as constants.
//!
//! SpookyHash is grouped into [`spooky_hash_v1`] and [`spooky_hash_v2`]; each
//! module provides 32-, 64-, and 128-bit variants. The 128-bit variants accept
//! two seeds and return the two hash words as a tuple.
//!
//! # Example
//!
//! ```
//! let name_hash = folly_hash::murmur_hash64(b"reader_pool", 0xc70f6907);
//! let combined_hash = folly_hash::hash_128_to_64(name_hash, 42);
//! # let _ = combined_hash;
//! ```

mod ffi;

pub const FNV32_HASH_START: u32 = 2_166_136_261;
pub const FNVA32_HASH_START: u32 = 2_166_136_261;
pub const FNV64_HASH_START: u64 = 14_695_981_039_346_656_037;
pub const FNVA64_HASH_START: u64 = 14_695_981_039_346_656_037;

pub fn hash_128_to_64(upper: u64, lower: u64) -> u64 {
    ffi::hash_128_to_64(upper, lower)
}

pub fn commutative_hash_128_to_64(upper: u64, lower: u64) -> u64 {
    ffi::commutative_hash_128_to_64(upper, lower)
}

pub fn twang_mix64(key: u64) -> u64 {
    ffi::twang_mix64(key)
}

pub fn twang_unmix64(key: u64) -> u64 {
    ffi::twang_unmix64(key)
}

pub fn twang_32from64(key: u64) -> u32 {
    ffi::twang_32from64(key)
}

pub fn jenkins_rev_mix32(key: u32) -> u32 {
    ffi::jenkins_rev_mix32(key)
}

pub fn jenkins_rev_unmix32(key: u32) -> u32 {
    ffi::jenkins_rev_unmix32(key)
}

pub fn std_compatible_hash(data: &[u8]) -> u64 {
    ffi::std_compatible_hash(data)
}

pub fn murmur_hash64(data: &[u8], seed: u64) -> u64 {
    ffi::murmur_hash64(data, seed)
}

pub fn fnv32_append_byte_broken(hash: u32, byte: u8) -> u32 {
    ffi::fnv32_append_byte_broken(hash, byte)
}

pub fn fnv32_buf_broken(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32_buf_broken(data, seed)
}

pub fn fnv32_broken(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32_broken(data, seed)
}

pub fn fnv32_append_byte_fixed(hash: u32, byte: u8) -> u32 {
    ffi::fnv32_append_byte_fixed(hash, byte)
}

pub fn fnv32_buf_fixed(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32_buf_fixed(data, seed)
}

pub fn fnv32_fixed(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32_fixed(data, seed)
}

pub fn fnva32_append_byte(hash: u32, byte: u8) -> u32 {
    ffi::fnva32_append_byte(hash, byte)
}

pub fn fnva32_buf(data: &[u8], seed: u32) -> u32 {
    ffi::fnva32_buf(data, seed)
}

pub fn fnva32(data: &[u8], seed: u32) -> u32 {
    ffi::fnva32(data, seed)
}

pub fn fnv64_append_byte_fixed(hash: u64, byte: u8) -> u64 {
    ffi::fnv64_append_byte_fixed(hash, byte)
}

pub fn fnv64_buf_fixed(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64_buf_fixed(data, seed)
}

pub fn fnv64_fixed(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64_fixed(data, seed)
}

pub fn fnv64_append_byte_broken(hash: u64, byte: u8) -> u64 {
    ffi::fnv64_append_byte_broken(hash, byte)
}

pub fn fnv64_buf_broken(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64_buf_broken(data, seed)
}

pub fn fnv64_broken(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64_broken(data, seed)
}

pub fn fnv32_append_byte(hash: u32, byte: u8) -> u32 {
    ffi::fnv32_append_byte(hash, byte)
}

pub fn fnv32_buf(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32_buf(data, seed)
}

pub fn fnv32(data: &[u8], seed: u32) -> u32 {
    ffi::fnv32(data, seed)
}

pub fn fnv64_append_byte(hash: u64, byte: u8) -> u64 {
    ffi::fnv64_append_byte(hash, byte)
}

pub fn fnv64_buf(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64_buf(data, seed)
}

pub fn fnv64(data: &[u8], seed: u64) -> u64 {
    ffi::fnv64(data, seed)
}

pub fn fnva64_append_byte(hash: u64, byte: u8) -> u64 {
    ffi::fnva64_append_byte(hash, byte)
}

pub fn fnva64_buf(data: &[u8], seed: u64) -> u64 {
    ffi::fnva64_buf(data, seed)
}

pub fn fnva64(data: &[u8], seed: u64) -> u64 {
    ffi::fnva64(data, seed)
}

pub fn hsieh_hash32_buf_constexpr(data: &[u8]) -> u32 {
    ffi::hsieh_hash32_buf_constexpr(data)
}

pub fn hsieh_hash32_buf(data: &[u8]) -> u32 {
    ffi::hsieh_hash32_buf(data)
}

pub fn hsieh_hash32(data: &[u8]) -> u32 {
    ffi::hsieh_hash32(data)
}

pub fn hsieh_hash32_str(data: &[u8]) -> u32 {
    ffi::hsieh_hash32_str(data)
}

pub mod spooky_hash_v1 {
    use super::ffi;

    pub fn hash32(data: &[u8], seed: u32) -> u32 {
        ffi::spooky_hash_v1_hash32(data, seed)
    }

    pub fn hash64(data: &[u8], seed: u64) -> u64 {
        ffi::spooky_hash_v1_hash64(data, seed)
    }

    pub fn hash128(data: &[u8], seed1: u64, seed2: u64) -> (u64, u64) {
        let hash = ffi::spooky_hash_v1_hash128(data, seed1, seed2);
        (hash.first, hash.second)
    }
}

pub mod spooky_hash_v2 {
    use super::ffi;

    pub fn hash32(data: &[u8], seed: u32) -> u32 {
        ffi::spooky_hash_v2_hash32(data, seed)
    }

    pub fn hash64(data: &[u8], seed: u64) -> u64 {
        ffi::spooky_hash_v2_hash64(data, seed)
    }

    pub fn hash128(data: &[u8], seed1: u64, seed2: u64) -> (u64, u64) {
        let hash = ffi::spooky_hash_v2_hash128(data, seed1, seed2);
        (hash.first, hash.second)
    }
}

#[cfg(test)]
mod tests;
