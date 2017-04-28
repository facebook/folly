/*
 * Copyright 2017 Facebook, Inc.
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

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <folly/Range.h>
#include <folly/io/IOBuf.h>

/**
 * Compression / decompression over IOBufs
 */

namespace folly { namespace io {

enum class CodecType {
  /**
   * This codec type is not defined; getCodec() will throw an exception
   * if used. Useful if deriving your own classes from Codec without
   * going through the getCodec() interface.
   */
  USER_DEFINED = 0,

  /**
   * Use no compression.
   * Levels supported: 0
   */
  NO_COMPRESSION = 1,

  /**
   * Use LZ4 compression.
   * Levels supported: 1 = fast, 2 = best; default = 1
   */
  LZ4 = 2,

  /**
   * Use Snappy compression.
   * Levels supported: 1
   */
  SNAPPY = 3,

  /**
   * Use zlib compression.
   * Levels supported: 0 = no compression, 1 = fast, ..., 9 = best; default = 6
   */
  ZLIB = 4,

  /**
   * Use LZ4 compression, prefixed with size (as Varint).
   */
  LZ4_VARINT_SIZE = 5,

  /**
   * Use LZMA2 compression.
   * Levels supported: 0 = no compression, 1 = fast, ..., 9 = best; default = 6
   */
  LZMA2 = 6,
  LZMA2_VARINT_SIZE = 7,

  /**
   * Use ZSTD compression.
   */
  ZSTD = 8,

  /**
   * Use gzip compression.  This is the same compression algorithm as ZLIB but
   * gzip-compressed files tend to be easier to work with from the command line.
   * Levels supported: 0 = no compression, 1 = fast, ..., 9 = best; default = 6
   */
  GZIP = 9,

  /**
   * Use LZ4 frame compression.
   * Levels supported: 0 = fast, 16 = best; default = 0
   */
  LZ4_FRAME = 10,

  /**
   * Use bzip2 compression.
   * Levels supported: 1 = fast, 9 = best; default = 9
   */
  BZIP2 = 11,

  NUM_CODEC_TYPES = 12,
};

class Codec {
 public:
  virtual ~Codec() { }

  /**
   * Return the maximum length of data that may be compressed with this codec.
   * NO_COMPRESSION and ZLIB support arbitrary lengths;
   * LZ4 supports up to 1.9GiB; SNAPPY supports up to 4GiB.
   * May return UNLIMITED_UNCOMPRESSED_LENGTH if unlimited.
   */
  uint64_t maxUncompressedLength() const;

  /**
   * Return the codec's type.
   */
  CodecType type() const { return type_; }

  /**
   * Does this codec need the exact uncompressed length on decompression?
   */
  bool needsUncompressedLength() const;

  /**
   * Compress data, returning an IOBuf (which may share storage with data).
   * Throws std::invalid_argument if data is larger than
   * maxUncompressedLength().
   *
   * Regardless of the behavior of the underlying compressor, compressing
   * an empty IOBuf chain will return an empty IOBuf chain.
   */
  std::unique_ptr<IOBuf> compress(const folly::IOBuf* data);

  /**
   * Compresses data. May involve additional copies compared to the overload
   * that takes and returns IOBufs. Has the same error semantics as the IOBuf
   * version.
   */
  std::string compress(StringPiece data);

  /**
   * Uncompress data. Throws std::runtime_error on decompression error.
   *
   * Some codecs (LZ4) require the exact uncompressed length; this is indicated
   * by needsUncompressedLength().
   *
   * For other codes (zlib), knowing the exact uncompressed length ahead of
   * time might be faster.
   *
   * Regardless of the behavior of the underlying compressor, uncompressing
   * an empty IOBuf chain will return an empty IOBuf chain.
   */
  static constexpr uint64_t UNKNOWN_UNCOMPRESSED_LENGTH = uint64_t(-1);
  static constexpr uint64_t UNLIMITED_UNCOMPRESSED_LENGTH = uint64_t(-2);

  std::unique_ptr<IOBuf> uncompress(
      const IOBuf* data,
      uint64_t uncompressedLength = UNKNOWN_UNCOMPRESSED_LENGTH);

  /**
   * Uncompresses data. May involve additional copies compared to the overload
   * that takes and returns IOBufs. Has the same error semantics as the IOBuf
   * version.
   */
  std::string uncompress(
      StringPiece data,
      uint64_t uncompressedLength = UNKNOWN_UNCOMPRESSED_LENGTH);

 protected:
  explicit Codec(CodecType type);

 public:
  /**
   * Returns a superset of the set of prefixes for which canUncompress() will
   * return true. A superset is allowed for optimizations in canUncompress()
   * based on other knowledge such as length. None of the prefixes may be empty.
   * default: No prefixes.
   */
  virtual std::vector<std::string> validPrefixes() const;

  /**
   * Returns true if the codec thinks it can uncompress the data.
   * If a codec doesn't have magic bytes at the beginning, like LZ4 and Snappy,
   * it can always return false.
   * default: Returns false.
   */
  virtual bool canUncompress(
      const folly::IOBuf* data,
      uint64_t uncompressedLength = UNKNOWN_UNCOMPRESSED_LENGTH) const;

 private:
  // default: no limits (save for special value UNKNOWN_UNCOMPRESSED_LENGTH)
  virtual uint64_t doMaxUncompressedLength() const;
  // default: doesn't need uncompressed length
  virtual bool doNeedsUncompressedLength() const;
  virtual std::unique_ptr<IOBuf> doCompress(const folly::IOBuf* data) = 0;
  virtual std::unique_ptr<IOBuf> doUncompress(const folly::IOBuf* data,
                                              uint64_t uncompressedLength) = 0;
  // default: an implementation is provided by default to wrap the strings into
  // IOBufs and delegate to the IOBuf methods. This incurs a copy of the output
  // from IOBuf to string. Implementers, at their discretion, can override
  // these methods to avoid the copy.
  virtual std::string doCompressString(StringPiece data);
  virtual std::string doUncompressString(
      StringPiece data,
      uint64_t uncompressedLength);

  CodecType type_;
};

constexpr int COMPRESSION_LEVEL_FASTEST = -1;
constexpr int COMPRESSION_LEVEL_DEFAULT = -2;
constexpr int COMPRESSION_LEVEL_BEST = -3;

/**
 * Return a codec for the given type. Throws on error.  The level
 * is a non-negative codec-dependent integer indicating the level of
 * compression desired, or one of the following constants:
 *
 * COMPRESSION_LEVEL_FASTEST is fastest (uses least CPU / memory,
 *   worst compression)
 * COMPRESSION_LEVEL_DEFAULT is the default (likely a tradeoff between
 *   FASTEST and BEST)
 * COMPRESSION_LEVEL_BEST is the best compression (uses most CPU / memory,
 *   best compression)
 *
 * When decompressing, the compression level is ignored. All codecs will
 * decompress all data compressed with the a codec of the same type, regardless
 * of compression level.
 */
std::unique_ptr<Codec> getCodec(CodecType type,
                                int level = COMPRESSION_LEVEL_DEFAULT);

/**
 * Returns a codec that can uncompress any of the given codec types as well as
 * {LZ4_FRAME, ZSTD, ZLIB, GZIP, LZMA2, BZIP2}. Appends each default codec to
 * customCodecs in order, so long as a codec with the same type() isn't already
 * present. When uncompress() is called, each codec's canUncompress() is called
 * in the order that they are given. Appended default codecs are checked last.
 * uncompress() is called on the first codec whose canUncompress() returns true.
 * An exception is thrown if no codec canUncompress() the data.
 * An exception is thrown if the chosen codec's uncompress() throws on the data.
 * An exception is thrown if compress() is called on the returned codec.
 *
 * Requirements are checked in debug mode and are as follows:
 * Let headers be the concatenation of every codec's validPrefixes().
 *  1. Each codec must override validPrefixes() and canUncompress().
 *  2. No codec's validPrefixes() may be empty.
 *  3. No header in headers may be empty.
 *  4. headers must not contain any duplicate elements.
 *  5. No strict non-empty prefix of any header in headers may be in headers.
 */
std::unique_ptr<Codec> getAutoUncompressionCodec(
    std::vector<std::unique_ptr<Codec>> customCodecs = {});

/**
 * Check if a specified codec is supported.
 */
bool hasCodec(CodecType type);

}}  // namespaces
