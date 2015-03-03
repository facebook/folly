/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/io/Compression.h>

#if FOLLY_HAVE_LIBLZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

#include <glog/logging.h>

#if FOLLY_HAVE_LIBSNAPPY
#include <snappy.h>
#include <snappy-sinksource.h>
#endif

#if FOLLY_HAVE_LIBZ
#include <zlib.h>
#endif

#if FOLLY_HAVE_LIBLZMA
#include <lzma.h>
#endif

#include <folly/Conv.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Varint.h>
#include <folly/io/Cursor.h>

namespace folly { namespace io {

Codec::Codec(CodecType type) : type_(type) { }

// Ensure consistent behavior in the nullptr case
std::unique_ptr<IOBuf> Codec::compress(const IOBuf* data) {
  uint64_t len = data->computeChainDataLength();
  if (len == 0) {
    return IOBuf::create(0);
  } else if (len > maxUncompressedLength()) {
    throw std::runtime_error("Codec: uncompressed length too large");
  }

  return doCompress(data);
}

std::unique_ptr<IOBuf> Codec::uncompress(const IOBuf* data,
                                         uint64_t uncompressedLength) {
  if (uncompressedLength == UNKNOWN_UNCOMPRESSED_LENGTH) {
    if (needsUncompressedLength()) {
      throw std::invalid_argument("Codec: uncompressed length required");
    }
  } else if (uncompressedLength > maxUncompressedLength()) {
    throw std::runtime_error("Codec: uncompressed length too large");
  }

  if (data->empty()) {
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != 0) {
      throw std::runtime_error("Codec: invalid uncompressed length");
    }
    return IOBuf::create(0);
  }

  return doUncompress(data, uncompressedLength);
}

bool Codec::needsUncompressedLength() const {
  return doNeedsUncompressedLength();
}

uint64_t Codec::maxUncompressedLength() const {
  return doMaxUncompressedLength();
}

bool Codec::doNeedsUncompressedLength() const {
  return false;
}

uint64_t Codec::doMaxUncompressedLength() const {
  return UNLIMITED_UNCOMPRESSED_LENGTH;
}

namespace {

/**
 * No compression
 */
class NoCompressionCodec FOLLY_FINAL : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit NoCompressionCodec(int level, CodecType type);

 private:
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) FOLLY_OVERRIDE;
};

std::unique_ptr<Codec> NoCompressionCodec::create(int level, CodecType type) {
  return make_unique<NoCompressionCodec>(level, type);
}

NoCompressionCodec::NoCompressionCodec(int level, CodecType type)
  : Codec(type) {
  DCHECK(type == CodecType::NO_COMPRESSION);
  switch (level) {
  case COMPRESSION_LEVEL_DEFAULT:
  case COMPRESSION_LEVEL_FASTEST:
  case COMPRESSION_LEVEL_BEST:
    level = 0;
  }
  if (level != 0) {
    throw std::invalid_argument(to<std::string>(
        "NoCompressionCodec: invalid level ", level));
  }
}

std::unique_ptr<IOBuf> NoCompressionCodec::doCompress(
    const IOBuf* data) {
  return data->clone();
}

std::unique_ptr<IOBuf> NoCompressionCodec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
      data->computeChainDataLength() != uncompressedLength) {
    throw std::runtime_error(to<std::string>(
        "NoCompressionCodec: invalid uncompressed length"));
  }
  return data->clone();
}

#if (FOLLY_HAVE_LIBLZ4 || FOLLY_HAVE_LIBLZMA)

namespace {

void encodeVarintToIOBuf(uint64_t val, folly::IOBuf* out) {
  DCHECK_GE(out->tailroom(), kMaxVarintLength64);
  out->append(encodeVarint(val, out->writableTail()));
}

inline uint64_t decodeVarintFromCursor(folly::io::Cursor& cursor) {
  uint64_t val = 0;
  int8_t b = 0;
  for (int shift = 0; shift <= 63; shift += 7) {
    b = cursor.read<int8_t>();
    val |= static_cast<uint64_t>(b & 0x7f) << shift;
    if (b >= 0) {
      break;
    }
  }
  if (b < 0) {
    throw std::invalid_argument("Invalid varint value. Too big.");
  }
  return val;
}

}  // namespace

#endif  // FOLLY_HAVE_LIBLZ4 || FOLLY_HAVE_LIBLZMA

#if FOLLY_HAVE_LIBLZ4

/**
 * LZ4 compression
 */
class LZ4Codec FOLLY_FINAL : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit LZ4Codec(int level, CodecType type);

 private:
  bool doNeedsUncompressedLength() const FOLLY_OVERRIDE;
  uint64_t doMaxUncompressedLength() const FOLLY_OVERRIDE;

  bool encodeSize() const { return type() == CodecType::LZ4_VARINT_SIZE; }

  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) FOLLY_OVERRIDE;

  bool highCompression_;
};

std::unique_ptr<Codec> LZ4Codec::create(int level, CodecType type) {
  return make_unique<LZ4Codec>(level, type);
}

LZ4Codec::LZ4Codec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::LZ4 || type == CodecType::LZ4_VARINT_SIZE);

  switch (level) {
  case COMPRESSION_LEVEL_FASTEST:
  case COMPRESSION_LEVEL_DEFAULT:
    level = 1;
    break;
  case COMPRESSION_LEVEL_BEST:
    level = 2;
    break;
  }
  if (level < 1 || level > 2) {
    throw std::invalid_argument(to<std::string>(
        "LZ4Codec: invalid level: ", level));
  }
  highCompression_ = (level > 1);
}

bool LZ4Codec::doNeedsUncompressedLength() const {
  return !encodeSize();
}

// The value comes from lz4.h in lz4-r117, but older versions of lz4 don't
// define LZ4_MAX_INPUT_SIZE (even though the max size is the same), so do it
// here.
#ifndef LZ4_MAX_INPUT_SIZE
# define LZ4_MAX_INPUT_SIZE 0x7E000000
#endif

uint64_t LZ4Codec::doMaxUncompressedLength() const {
  return LZ4_MAX_INPUT_SIZE;
}

std::unique_ptr<IOBuf> LZ4Codec::doCompress(const IOBuf* data) {
  std::unique_ptr<IOBuf> clone;
  if (data->isChained()) {
    // LZ4 doesn't support streaming, so we have to coalesce
    clone = data->clone();
    clone->coalesce();
    data = clone.get();
  }

  uint32_t extraSize = encodeSize() ? kMaxVarintLength64 : 0;
  auto out = IOBuf::create(extraSize + LZ4_compressBound(data->length()));
  if (encodeSize()) {
    encodeVarintToIOBuf(data->length(), out.get());
  }

  int n;
  if (highCompression_) {
    n = LZ4_compressHC(reinterpret_cast<const char*>(data->data()),
                       reinterpret_cast<char*>(out->writableTail()),
                       data->length());
  } else {
    n = LZ4_compress(reinterpret_cast<const char*>(data->data()),
                     reinterpret_cast<char*>(out->writableTail()),
                     data->length());
  }

  CHECK_GE(n, 0);
  CHECK_LE(n, out->capacity());

  out->append(n);
  return out;
}

std::unique_ptr<IOBuf> LZ4Codec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  std::unique_ptr<IOBuf> clone;
  if (data->isChained()) {
    // LZ4 doesn't support streaming, so we have to coalesce
    clone = data->clone();
    clone->coalesce();
    data = clone.get();
  }

  folly::io::Cursor cursor(data);
  uint64_t actualUncompressedLength;
  if (encodeSize()) {
    actualUncompressedLength = decodeVarintFromCursor(cursor);
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != actualUncompressedLength) {
      throw std::runtime_error("LZ4Codec: invalid uncompressed length");
    }
  } else {
    actualUncompressedLength = uncompressedLength;
    if (actualUncompressedLength == UNKNOWN_UNCOMPRESSED_LENGTH ||
        actualUncompressedLength > maxUncompressedLength()) {
      throw std::runtime_error("LZ4Codec: invalid uncompressed length");
    }
  }

  auto p = cursor.peek();
  auto out = IOBuf::create(actualUncompressedLength);
  int n = LZ4_decompress_safe(reinterpret_cast<const char*>(p.first),
                              reinterpret_cast<char*>(out->writableTail()),
                              p.second,
                              actualUncompressedLength);

  if (n < 0 || uint64_t(n) != actualUncompressedLength) {
    throw std::runtime_error(to<std::string>(
        "LZ4 decompression returned invalid value ", n));
  }
  out->append(actualUncompressedLength);
  return out;
}

#endif  // FOLLY_HAVE_LIBLZ4

#if FOLLY_HAVE_LIBSNAPPY

/**
 * Snappy compression
 */

/**
 * Implementation of snappy::Source that reads from a IOBuf chain.
 */
class IOBufSnappySource FOLLY_FINAL : public snappy::Source {
 public:
  explicit IOBufSnappySource(const IOBuf* data);
  size_t Available() const FOLLY_OVERRIDE;
  const char* Peek(size_t* len) FOLLY_OVERRIDE;
  void Skip(size_t n) FOLLY_OVERRIDE;
 private:
  size_t available_;
  io::Cursor cursor_;
};

IOBufSnappySource::IOBufSnappySource(const IOBuf* data)
  : available_(data->computeChainDataLength()),
    cursor_(data) {
}

size_t IOBufSnappySource::Available() const {
  return available_;
}

const char* IOBufSnappySource::Peek(size_t* len) {
  auto p = cursor_.peek();
  *len = p.second;
  return reinterpret_cast<const char*>(p.first);
}

void IOBufSnappySource::Skip(size_t n) {
  CHECK_LE(n, available_);
  cursor_.skip(n);
  available_ -= n;
}

class SnappyCodec FOLLY_FINAL : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit SnappyCodec(int level, CodecType type);

 private:
  uint64_t doMaxUncompressedLength() const FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) FOLLY_OVERRIDE;
};

std::unique_ptr<Codec> SnappyCodec::create(int level, CodecType type) {
  return make_unique<SnappyCodec>(level, type);
}

SnappyCodec::SnappyCodec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::SNAPPY);
  switch (level) {
  case COMPRESSION_LEVEL_FASTEST:
  case COMPRESSION_LEVEL_DEFAULT:
  case COMPRESSION_LEVEL_BEST:
    level = 1;
  }
  if (level != 1) {
    throw std::invalid_argument(to<std::string>(
        "SnappyCodec: invalid level: ", level));
  }
}

uint64_t SnappyCodec::doMaxUncompressedLength() const {
  // snappy.h uses uint32_t for lengths, so there's that.
  return std::numeric_limits<uint32_t>::max();
}

std::unique_ptr<IOBuf> SnappyCodec::doCompress(const IOBuf* data) {
  IOBufSnappySource source(data);
  auto out =
    IOBuf::create(snappy::MaxCompressedLength(source.Available()));

  snappy::UncheckedByteArraySink sink(reinterpret_cast<char*>(
      out->writableTail()));

  size_t n = snappy::Compress(&source, &sink);

  CHECK_LE(n, out->capacity());
  out->append(n);
  return out;
}

std::unique_ptr<IOBuf> SnappyCodec::doUncompress(const IOBuf* data,
                                                 uint64_t uncompressedLength) {
  uint32_t actualUncompressedLength = 0;

  {
    IOBufSnappySource source(data);
    if (!snappy::GetUncompressedLength(&source, &actualUncompressedLength)) {
      throw std::runtime_error("snappy::GetUncompressedLength failed");
    }
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != actualUncompressedLength) {
      throw std::runtime_error("snappy: invalid uncompressed length");
    }
  }

  auto out = IOBuf::create(actualUncompressedLength);

  {
    IOBufSnappySource source(data);
    if (!snappy::RawUncompress(&source,
                               reinterpret_cast<char*>(out->writableTail()))) {
      throw std::runtime_error("snappy::RawUncompress failed");
    }
  }

  out->append(actualUncompressedLength);
  return out;
}

#endif  // FOLLY_HAVE_LIBSNAPPY

#if FOLLY_HAVE_LIBZ
/**
 * Zlib codec
 */
class ZlibCodec FOLLY_FINAL : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit ZlibCodec(int level, CodecType type);

 private:
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) FOLLY_OVERRIDE;

  std::unique_ptr<IOBuf> addOutputBuffer(z_stream* stream, uint32_t length);
  bool doInflate(z_stream* stream, IOBuf* head, uint32_t bufferLength);

  int level_;
};

std::unique_ptr<Codec> ZlibCodec::create(int level, CodecType type) {
  return make_unique<ZlibCodec>(level, type);
}

ZlibCodec::ZlibCodec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::ZLIB);
  switch (level) {
  case COMPRESSION_LEVEL_FASTEST:
    level = 1;
    break;
  case COMPRESSION_LEVEL_DEFAULT:
    level = Z_DEFAULT_COMPRESSION;
    break;
  case COMPRESSION_LEVEL_BEST:
    level = 9;
    break;
  }
  if (level != Z_DEFAULT_COMPRESSION && (level < 0 || level > 9)) {
    throw std::invalid_argument(to<std::string>(
        "ZlibCodec: invalid level: ", level));
  }
  level_ = level;
}

std::unique_ptr<IOBuf> ZlibCodec::addOutputBuffer(z_stream* stream,
                                                  uint32_t length) {
  CHECK_EQ(stream->avail_out, 0);

  auto buf = IOBuf::create(length);
  buf->append(length);

  stream->next_out = buf->writableData();
  stream->avail_out = buf->length();

  return buf;
}

bool ZlibCodec::doInflate(z_stream* stream,
                          IOBuf* head,
                          uint32_t bufferLength) {
  if (stream->avail_out == 0) {
    head->prependChain(addOutputBuffer(stream, bufferLength));
  }

  int rc = inflate(stream, Z_NO_FLUSH);

  switch (rc) {
  case Z_OK:
    break;
  case Z_STREAM_END:
    return true;
  case Z_BUF_ERROR:
  case Z_NEED_DICT:
  case Z_DATA_ERROR:
  case Z_MEM_ERROR:
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: inflate error: ", rc, ": ", stream->msg));
  default:
    CHECK(false) << rc << ": " << stream->msg;
  }

  return false;
}

std::unique_ptr<IOBuf> ZlibCodec::doCompress(const IOBuf* data) {
  z_stream stream;
  stream.zalloc = nullptr;
  stream.zfree = nullptr;
  stream.opaque = nullptr;

  int rc = deflateInit(&stream, level_);
  if (rc != Z_OK) {
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: deflateInit error: ", rc, ": ", stream.msg));
  }

  stream.next_in = stream.next_out = nullptr;
  stream.avail_in = stream.avail_out = 0;
  stream.total_in = stream.total_out = 0;

  bool success = false;

  SCOPE_EXIT {
    int rc = deflateEnd(&stream);
    // If we're here because of an exception, it's okay if some data
    // got dropped.
    CHECK(rc == Z_OK || (!success && rc == Z_DATA_ERROR))
      << rc << ": " << stream.msg;
  };

  uint64_t uncompressedLength = data->computeChainDataLength();
  uint64_t maxCompressedLength = deflateBound(&stream, uncompressedLength);

  // Max 64MiB in one go
  constexpr uint32_t maxSingleStepLength = uint32_t(64) << 20;    // 64MiB
  constexpr uint32_t defaultBufferLength = uint32_t(4) << 20;     // 4MiB

  auto out = addOutputBuffer(
      &stream,
      (maxCompressedLength <= maxSingleStepLength ?
       maxCompressedLength :
       defaultBufferLength));

  for (auto& range : *data) {
    uint64_t remaining = range.size();
    uint64_t written = 0;
    while (remaining) {
      uint32_t step = (remaining > maxSingleStepLength ?
                       maxSingleStepLength : remaining);
      stream.next_in = const_cast<uint8_t*>(range.data() + written);
      stream.avail_in = step;
      remaining -= step;
      written += step;

      while (stream.avail_in != 0) {
        if (stream.avail_out == 0) {
          out->prependChain(addOutputBuffer(&stream, defaultBufferLength));
        }

        rc = deflate(&stream, Z_NO_FLUSH);

        CHECK_EQ(rc, Z_OK) << stream.msg;
      }
    }
  }

  do {
    if (stream.avail_out == 0) {
      out->prependChain(addOutputBuffer(&stream, defaultBufferLength));
    }

    rc = deflate(&stream, Z_FINISH);
  } while (rc == Z_OK);

  CHECK_EQ(rc, Z_STREAM_END) << stream.msg;

  out->prev()->trimEnd(stream.avail_out);

  success = true;  // we survived

  return out;
}

std::unique_ptr<IOBuf> ZlibCodec::doUncompress(const IOBuf* data,
                                               uint64_t uncompressedLength) {
  z_stream stream;
  stream.zalloc = nullptr;
  stream.zfree = nullptr;
  stream.opaque = nullptr;

  int rc = inflateInit(&stream);
  if (rc != Z_OK) {
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: inflateInit error: ", rc, ": ", stream.msg));
  }

  stream.next_in = stream.next_out = nullptr;
  stream.avail_in = stream.avail_out = 0;
  stream.total_in = stream.total_out = 0;

  bool success = false;

  SCOPE_EXIT {
    int rc = inflateEnd(&stream);
    // If we're here because of an exception, it's okay if some data
    // got dropped.
    CHECK(rc == Z_OK || (!success && rc == Z_DATA_ERROR))
      << rc << ": " << stream.msg;
  };

  // Max 64MiB in one go
  constexpr uint32_t maxSingleStepLength = uint32_t(64) << 20;    // 64MiB
  constexpr uint32_t defaultBufferLength = uint32_t(4) << 20;     // 4MiB

  auto out = addOutputBuffer(
      &stream,
      ((uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength <= maxSingleStepLength) ?
       uncompressedLength :
       defaultBufferLength));

  bool streamEnd = false;
  for (auto& range : *data) {
    if (range.empty()) {
      continue;
    }

    stream.next_in = const_cast<uint8_t*>(range.data());
    stream.avail_in = range.size();

    while (stream.avail_in != 0) {
      if (streamEnd) {
        throw std::runtime_error(to<std::string>(
            "ZlibCodec: junk after end of data"));
      }

      streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
    }
  }

  while (!streamEnd) {
    streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
  }

  out->prev()->trimEnd(stream.avail_out);

  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
      uncompressedLength != stream.total_out) {
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: invalid uncompressed length"));
  }

  success = true;  // we survived

  return out;
}

#endif  // FOLLY_HAVE_LIBZ

#if FOLLY_HAVE_LIBLZMA

/**
 * LZMA2 compression
 */
class LZMA2Codec FOLLY_FINAL : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit LZMA2Codec(int level, CodecType type);

 private:
  bool doNeedsUncompressedLength() const FOLLY_OVERRIDE;
  uint64_t doMaxUncompressedLength() const FOLLY_OVERRIDE;

  bool encodeSize() const { return type() == CodecType::LZMA2_VARINT_SIZE; }

  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) FOLLY_OVERRIDE;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) FOLLY_OVERRIDE;

  std::unique_ptr<IOBuf> addOutputBuffer(lzma_stream* stream, size_t length);
  bool doInflate(lzma_stream* stream, IOBuf* head, size_t bufferLength);

  int level_;
};

std::unique_ptr<Codec> LZMA2Codec::create(int level, CodecType type) {
  return make_unique<LZMA2Codec>(level, type);
}

LZMA2Codec::LZMA2Codec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::LZMA2 || type == CodecType::LZMA2_VARINT_SIZE);
  switch (level) {
  case COMPRESSION_LEVEL_FASTEST:
    level = 0;
    break;
  case COMPRESSION_LEVEL_DEFAULT:
    level = LZMA_PRESET_DEFAULT;
    break;
  case COMPRESSION_LEVEL_BEST:
    level = 9;
    break;
  }
  if (level < 0 || level > 9) {
    throw std::invalid_argument(to<std::string>(
        "LZMA2Codec: invalid level: ", level));
  }
  level_ = level;
}

bool LZMA2Codec::doNeedsUncompressedLength() const {
  return !encodeSize();
}

uint64_t LZMA2Codec::doMaxUncompressedLength() const {
  // From lzma/base.h: "Stream is roughly 8 EiB (2^63 bytes)"
  return uint64_t(1) << 63;
}

std::unique_ptr<IOBuf> LZMA2Codec::addOutputBuffer(
    lzma_stream* stream,
    size_t length) {

  CHECK_EQ(stream->avail_out, 0);

  auto buf = IOBuf::create(length);
  buf->append(length);

  stream->next_out = buf->writableData();
  stream->avail_out = buf->length();

  return buf;
}

std::unique_ptr<IOBuf> LZMA2Codec::doCompress(const IOBuf* data) {
  lzma_ret rc;
  lzma_stream stream = LZMA_STREAM_INIT;

  rc = lzma_easy_encoder(&stream, level_, LZMA_CHECK_NONE);
  if (rc != LZMA_OK) {
    throw std::runtime_error(folly::to<std::string>(
      "LZMA2Codec: lzma_easy_encoder error: ", rc));
  }

  SCOPE_EXIT { lzma_end(&stream); };

  uint64_t uncompressedLength = data->computeChainDataLength();
  uint64_t maxCompressedLength = lzma_stream_buffer_bound(uncompressedLength);

  // Max 64MiB in one go
  constexpr uint32_t maxSingleStepLength = uint32_t(64) << 20;    // 64MiB
  constexpr uint32_t defaultBufferLength = uint32_t(4) << 20;     // 4MiB

  auto out = addOutputBuffer(
    &stream,
    (maxCompressedLength <= maxSingleStepLength ?
     maxCompressedLength :
     defaultBufferLength));

  if (encodeSize()) {
    auto size = IOBuf::createCombined(kMaxVarintLength64);
    encodeVarintToIOBuf(uncompressedLength, size.get());
    size->appendChain(std::move(out));
    out = std::move(size);
  }

  for (auto& range : *data) {
    if (range.empty()) {
      continue;
    }

    stream.next_in = const_cast<uint8_t*>(range.data());
    stream.avail_in = range.size();

    while (stream.avail_in != 0) {
      if (stream.avail_out == 0) {
        out->prependChain(addOutputBuffer(&stream, defaultBufferLength));
      }

      rc = lzma_code(&stream, LZMA_RUN);

      if (rc != LZMA_OK) {
        throw std::runtime_error(folly::to<std::string>(
          "LZMA2Codec: lzma_code error: ", rc));
      }
    }
  }

  do {
    if (stream.avail_out == 0) {
      out->prependChain(addOutputBuffer(&stream, defaultBufferLength));
    }

    rc = lzma_code(&stream, LZMA_FINISH);
  } while (rc == LZMA_OK);

  if (rc != LZMA_STREAM_END) {
    throw std::runtime_error(folly::to<std::string>(
      "LZMA2Codec: lzma_code ended with error: ", rc));
  }

  out->prev()->trimEnd(stream.avail_out);

  return out;
}

bool LZMA2Codec::doInflate(lzma_stream* stream,
                          IOBuf* head,
                          size_t bufferLength) {
  if (stream->avail_out == 0) {
    head->prependChain(addOutputBuffer(stream, bufferLength));
  }

  lzma_ret rc = lzma_code(stream, LZMA_RUN);

  switch (rc) {
  case LZMA_OK:
    break;
  case LZMA_STREAM_END:
    return true;
  default:
    throw std::runtime_error(to<std::string>(
        "LZMA2Codec: lzma_code error: ", rc));
  }

  return false;
}

std::unique_ptr<IOBuf> LZMA2Codec::doUncompress(const IOBuf* data,
                                               uint64_t uncompressedLength) {
  lzma_ret rc;
  lzma_stream stream = LZMA_STREAM_INIT;

  rc = lzma_auto_decoder(&stream, std::numeric_limits<uint64_t>::max(), 0);
  if (rc != LZMA_OK) {
    throw std::runtime_error(folly::to<std::string>(
      "LZMA2Codec: lzma_auto_decoder error: ", rc));
  }

  SCOPE_EXIT { lzma_end(&stream); };

  // Max 64MiB in one go
  constexpr uint32_t maxSingleStepLength = uint32_t(64) << 20;    // 64MiB
  constexpr uint32_t defaultBufferLength = uint32_t(4) << 20;     // 4MiB

  folly::io::Cursor cursor(data);
  uint64_t actualUncompressedLength;
  if (encodeSize()) {
    actualUncompressedLength = decodeVarintFromCursor(cursor);
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != actualUncompressedLength) {
      throw std::runtime_error("LZMA2Codec: invalid uncompressed length");
    }
  } else {
    actualUncompressedLength = uncompressedLength;
    DCHECK_NE(actualUncompressedLength, UNKNOWN_UNCOMPRESSED_LENGTH);
  }

  auto out = addOutputBuffer(
      &stream,
      (actualUncompressedLength <= maxSingleStepLength ?
       actualUncompressedLength :
       defaultBufferLength));

  bool streamEnd = false;
  auto buf = cursor.peek();
  while (buf.second != 0) {
    stream.next_in = const_cast<uint8_t*>(buf.first);
    stream.avail_in = buf.second;

    while (stream.avail_in != 0) {
      if (streamEnd) {
        throw std::runtime_error(to<std::string>(
            "LZMA2Codec: junk after end of data"));
      }

      streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
    }

    cursor.skip(buf.second);
    buf = cursor.peek();
  }

  while (!streamEnd) {
    streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
  }

  out->prev()->trimEnd(stream.avail_out);

  if (actualUncompressedLength != stream.total_out) {
    throw std::runtime_error(to<std::string>(
        "LZMA2Codec: invalid uncompressed length"));
  }

  return out;
}

#endif  // FOLLY_HAVE_LIBLZMA

}  // namespace

std::unique_ptr<Codec> getCodec(CodecType type, int level) {
  typedef std::unique_ptr<Codec> (*CodecFactory)(int, CodecType);

  static CodecFactory codecFactories[
    static_cast<size_t>(CodecType::NUM_CODEC_TYPES)] = {
    nullptr,  // USER_DEFINED
    NoCompressionCodec::create,

#if FOLLY_HAVE_LIBLZ4
    LZ4Codec::create,
#else
    nullptr,
#endif

#if FOLLY_HAVE_LIBSNAPPY
    SnappyCodec::create,
#else
    nullptr,
#endif

#if FOLLY_HAVE_LIBZ
    ZlibCodec::create,
#else
    nullptr,
#endif

#if FOLLY_HAVE_LIBLZ4
    LZ4Codec::create,
#else
    nullptr,
#endif

#if FOLLY_HAVE_LIBLZMA
    LZMA2Codec::create,
    LZMA2Codec::create,
#else
    nullptr,
    nullptr,
#endif
  };

  size_t idx = static_cast<size_t>(type);
  if (idx >= static_cast<size_t>(CodecType::NUM_CODEC_TYPES)) {
    throw std::invalid_argument(to<std::string>(
        "Compression type ", idx, " not supported"));
  }
  auto factory = codecFactories[idx];
  if (!factory) {
    throw std::invalid_argument(to<std::string>(
        "Compression type ", idx, " not supported"));
  }
  auto codec = (*factory)(level, type);
  DCHECK_EQ(static_cast<size_t>(codec->type()), idx);
  return codec;
}

}}  // namespaces
