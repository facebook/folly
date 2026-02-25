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

use bytes::Bytes;
use cxx::UniquePtr;
use iobuf::IOBuf;
use iobuf::IOBufShared;

#[cxx::bridge]
mod bridge {
    #[namespace = "facebook::folly_rust::compression"]
    unsafe extern "C++" {
        include!("folly/rust/compression/compression.h");

        #[namespace = "folly"]
        type IOBuf = iobuf::IOBuf;

        #[namespace = "folly::compression"]
        #[cxx_name = "Codec"]
        type CodecFfi;

        fn has_codec(codec_type: i32) -> bool;

        fn create_codec(codec_type: i32) -> Result<UniquePtr<CodecFfi>>;

        fn compress(codec: Pin<&mut CodecFfi>, data: &[u8]) -> Result<UniquePtr<IOBuf>>;

        fn uncompress(codec: Pin<&mut CodecFfi>, data: &[u8]) -> Result<UniquePtr<IOBuf>>;

        fn uncompress_length(
            codec: Pin<&mut CodecFfi>,
            data: &[u8],
            uncompressed_length: u64,
        ) -> Result<UniquePtr<IOBuf>>;

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

impl TryFrom<i32> for CodecType {
    type Error = i32;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(CodecType::NoCompression),
            2 => Ok(CodecType::Lz4),
            3 => Ok(CodecType::Snappy),
            4 => Ok(CodecType::Zlib),
            5 => Ok(CodecType::Lz4VarintSize),
            6 => Ok(CodecType::Lzma2),
            7 => Ok(CodecType::Lzma2VarintSize),
            8 => Ok(CodecType::Zstd),
            9 => Ok(CodecType::Gzip),
            10 => Ok(CodecType::Lz4Frame),
            11 => Ok(CodecType::Bzip2),
            12 => Ok(CodecType::ZstdFast),
            _ => Err(value),
        }
    }
}

/// Check whether a given codec type is supported.
pub fn has_codec(codec_type: CodecType) -> bool {
    bridge::has_codec(codec_type as i32)
}

pub struct Codec {
    inner: cxx::UniquePtr<bridge::CodecFfi>,
}

fn iobuf_to_bytes(iobuf: UniquePtr<IOBuf>) -> Bytes {
    // This is zero-copy.
    //
    // When the IOBuf is contiguous and unshared (always true in our case),
    // Bytes::from(IOBufShared) uses from_owner() to hold the IOBufShared directly and
    // point at the C++-allocated buffer — zero-copy. The buffer is freed when Bytes drops.
    // Falls back to memcpy only for chained or shared IOBufs.
    Bytes::from(IOBufShared::from(iobuf))
}

impl Codec {
    pub fn new(codec_type: CodecType) -> Result<Self, CompressionError> {
        let inner = bridge::create_codec(codec_type as i32)?;
        Ok(Self { inner })
    }

    pub fn compress(&mut self, data: &[u8]) -> Result<Bytes, CompressionError> {
        let iobuf = bridge::compress(self.inner.pin_mut(), data)?;
        Ok(iobuf_to_bytes(iobuf))
    }

    pub fn uncompress(
        &mut self,
        data: &[u8],
        uncompressed_length: Option<u64>,
    ) -> Result<Bytes, CompressionError> {
        let iobuf = match uncompressed_length {
            Some(len) => bridge::uncompress_length(self.inner.pin_mut(), data, len)?,
            None => bridge::uncompress(self.inner.pin_mut(), data)?,
        };
        Ok(iobuf_to_bytes(iobuf))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_has_codec() {
        assert!(has_codec(CodecType::NoCompression));
        assert!(has_codec(CodecType::Lz4));
        assert!(has_codec(CodecType::Snappy));
        assert!(has_codec(CodecType::Zlib));
        assert!(has_codec(CodecType::Zstd));
        assert!(has_codec(CodecType::Gzip));
        assert!(has_codec(CodecType::Lz4Frame));
    }

    #[test]
    fn test_codec_type_try_from() {
        assert_eq!(CodecType::try_from(1), Ok(CodecType::NoCompression));
        assert_eq!(CodecType::try_from(8), Ok(CodecType::Zstd));
        assert_eq!(CodecType::try_from(12), Ok(CodecType::ZstdFast));
        assert_eq!(CodecType::try_from(0), Err(0));
        assert_eq!(CodecType::try_from(99), Err(99));
        assert_eq!(CodecType::try_from(-1), Err(-1));
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
            CodecType::Lz4VarintSize,
            CodecType::Lzma2,
            CodecType::Lzma2VarintSize,
        ];
        let requires_length = [CodecType::Lz4, CodecType::Snappy, CodecType::Lz4VarintSize];
        let original = b"Hello, folly compression from Rust! This is a test string that should be compressible.";

        for ct in types {
            if !has_codec(ct) {
                continue;
            }
            let mut codec = Codec::new(ct).expect("create codec failed");
            let compressed = codec
                .compress(original.as_slice())
                .expect("compress failed");
            let decompressed = codec
                .uncompress(&compressed, Some(original.len() as u64))
                .expect("uncompress failed");
            assert_eq!(
                &decompressed[..],
                &original[..],
                "Roundtrip failed for {:?}",
                ct
            );

            if !requires_length.contains(&ct) {
                let decompressed = codec
                    .uncompress(&compressed, None)
                    .expect("uncompress (no length) failed");
                assert_eq!(
                    &decompressed[..],
                    &original[..],
                    "Roundtrip (no length) failed for {:?}",
                    ct
                );
            }
        }
    }

    #[test]
    fn test_compress_empty_data() {
        let original: &[u8] = b"";
        let mut codec = Codec::new(CodecType::Zstd).expect("create codec failed");
        let compressed = codec
            .compress(original)
            .expect("compress empty data failed");
        let decompressed = codec
            .uncompress(&compressed, Some(original.len() as u64))
            .expect("uncompress empty data failed");
        assert_eq!(&decompressed[..], original);
    }

    #[test]
    fn test_error_message() {
        let mut codec = Codec::new(CodecType::Zstd).expect("create codec failed");
        let err = codec.uncompress(b"not valid compressed data", Some(100));
        assert!(err.is_err());
        let msg = err.expect_err("expected decompression error").to_string();
        assert!(!msg.is_empty());
    }

    #[test]
    fn test_codec_reuse() {
        let mut codec = Codec::new(CodecType::Zstd).expect("failed to create Zstd codec");
        for i in 0..3 {
            let original = format!("test data for codec reuse, iteration {i}");
            let compressed = codec
                .compress(original.as_bytes())
                .expect("compress failed");
            let decompressed = codec
                .uncompress(&compressed, Some(original.len() as u64))
                .expect("uncompress failed");
            assert_eq!(&decompressed[..], original.as_bytes());
        }
    }
}
