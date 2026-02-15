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

use std::fmt;

#[cxx::bridge]
mod bridge {
    #[namespace = "facebook::folly_rust::compression"]
    unsafe extern "C++" {
        include!("folly/rust/compression/compression.h");

        fn has_codec(codec_type: i32) -> bool;

        fn compress_bytes(codec_type: i32, data: &[u8]) -> Result<UniquePtr<CxxString>>;

        fn uncompress_bytes(
            codec_type: i32,
            data: &[u8],
            uncompressed_length: u64,
        ) -> Result<UniquePtr<CxxString>>;
    }
}

/// Error returned by compression/decompression operations.
#[derive(Debug)]
pub struct CompressionError(String);

impl fmt::Display for CompressionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for CompressionError {}

impl From<cxx::Exception> for CompressionError {
    fn from(e: cxx::Exception) -> Self {
        CompressionError(e.what().to_owned())
    }
}

/// Mirror of `folly::compression::CodecType`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum CodecType {
    NoCompression = 1,
    Lz4 = 2,
    Snappy = 3,
    Zlib = 4,
    Lz4VarintSize = 5,
    Lzma2 = 6,
    Lzma2VarintSize = 7,
    Zstd = 8,
    Gzip = 9,
    Lz4Frame = 10,
    Bzip2 = 11,
    ZstdFast = 12,
}

impl CodecType {
    /// Convert from the raw integer value used in C++.
    /// Returns `None` for unrecognized values.
    pub fn from_i32(value: i32) -> Option<Self> {
        match value {
            1 => Some(CodecType::NoCompression),
            2 => Some(CodecType::Lz4),
            3 => Some(CodecType::Snappy),
            4 => Some(CodecType::Zlib),
            5 => Some(CodecType::Lz4VarintSize),
            6 => Some(CodecType::Lzma2),
            7 => Some(CodecType::Lzma2VarintSize),
            8 => Some(CodecType::Zstd),
            9 => Some(CodecType::Gzip),
            10 => Some(CodecType::Lz4Frame),
            11 => Some(CodecType::Bzip2),
            12 => Some(CodecType::ZstdFast),
            _ => None,
        }
    }
}

/// Check whether a given codec type is supported.
pub fn has_codec(codec_type: CodecType) -> bool {
    bridge::has_codec(codec_type as i32)
}

/// Compress a byte slice using the specified codec.
pub fn compress(codec_type: CodecType, data: &[u8]) -> Result<Vec<u8>, CompressionError> {
    let result = bridge::compress_bytes(codec_type as i32, data)?;
    Ok(result.as_bytes().to_vec())
}

/// Uncompress a byte slice using the specified codec.
pub fn uncompress(
    codec_type: CodecType,
    data: &[u8],
    uncompressed_length: u64,
) -> Result<Vec<u8>, CompressionError> {
    let result = bridge::uncompress_bytes(codec_type as i32, data, uncompressed_length)?;
    Ok(result.as_bytes().to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_has_codec() {
        assert!(has_codec(CodecType::NoCompression));
        assert!(has_codec(CodecType::Zstd));
        assert!(has_codec(CodecType::Snappy));
        assert!(has_codec(CodecType::Zlib));
        assert!(has_codec(CodecType::Gzip));
        assert!(has_codec(CodecType::Lz4));
        assert!(has_codec(CodecType::Lz4Frame));
    }

    #[test]
    fn test_codec_type_from_i32() {
        assert_eq!(CodecType::from_i32(1), Some(CodecType::NoCompression));
        assert_eq!(CodecType::from_i32(8), Some(CodecType::Zstd));
        assert_eq!(CodecType::from_i32(12), Some(CodecType::ZstdFast));
        assert_eq!(CodecType::from_i32(0), None);
        assert_eq!(CodecType::from_i32(99), None);
        assert_eq!(CodecType::from_i32(-1), None);
    }

    #[test]
    fn test_compress_uncompress_roundtrip() {
        let types = [
            CodecType::NoCompression,
            CodecType::Lz4,
            CodecType::Snappy,
            CodecType::Zlib,
            CodecType::Zstd,
            CodecType::Gzip,
            CodecType::Lz4Frame,
            CodecType::Bzip2,
            CodecType::ZstdFast,
        ];
        let original = b"Hello, folly compression from Rust! This is a test string that should be compressible.";

        for ct in types {
            if !has_codec(ct) {
                continue;
            }
            let compressed = compress(ct, original.as_slice()).unwrap();
            let decompressed = uncompress(ct, &compressed, original.len() as u64).unwrap();
            assert_eq!(
                decompressed,
                original.to_vec(),
                "Roundtrip failed for {:?}",
                ct
            );
        }
    }

    #[test]
    fn test_compress_empty_data() {
        let original: &[u8] = b"";
        let compressed = compress(CodecType::Zstd, original).unwrap();
        let decompressed = uncompress(CodecType::Zstd, &compressed, original.len() as u64).unwrap();
        assert_eq!(decompressed, original.to_vec());
    }

    #[test]
    fn test_error_message() {
        let err = uncompress(CodecType::Zstd, b"not valid compressed data", 100);
        assert!(err.is_err());
        let msg = err.unwrap_err().to_string();
        assert!(!msg.is_empty());
    }
}
