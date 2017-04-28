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

#include <folly/io/Compression.h>

#if FOLLY_HAVE_LIBLZ4
#include <lz4.h>
#include <lz4hc.h>
#if LZ4_VERSION_NUMBER >= 10301
#include <lz4frame.h>
#endif
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

#if FOLLY_HAVE_LIBZSTD
#include <zstd.h>
#endif

#if FOLLY_HAVE_LIBBZ2
#include <bzlib.h>
#endif

#include <folly/Bits.h>
#include <folly/Conv.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Varint.h>
#include <folly/io/Cursor.h>
#include <algorithm>
#include <unordered_set>

namespace folly { namespace io {

Codec::Codec(CodecType type) : type_(type) { }

// Ensure consistent behavior in the nullptr case
std::unique_ptr<IOBuf> Codec::compress(const IOBuf* data) {
  uint64_t len = data->computeChainDataLength();
  if (len == 0) {
    return IOBuf::create(0);
  }
  if (len > maxUncompressedLength()) {
    throw std::runtime_error("Codec: uncompressed length too large");
  }

  return doCompress(data);
}

std::string Codec::compress(const StringPiece data) {
  const uint64_t len = data.size();
  if (len == 0) {
    return "";
  }
  if (len > maxUncompressedLength()) {
    throw std::runtime_error("Codec: uncompressed length too large");
  }

  return doCompressString(data);
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

std::string Codec::uncompress(
    const StringPiece data,
    uint64_t uncompressedLength) {
  if (uncompressedLength == UNKNOWN_UNCOMPRESSED_LENGTH) {
    if (needsUncompressedLength()) {
      throw std::invalid_argument("Codec: uncompressed length required");
    }
  } else if (uncompressedLength > maxUncompressedLength()) {
    throw std::runtime_error("Codec: uncompressed length too large");
  }

  if (data.empty()) {
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != 0) {
      throw std::runtime_error("Codec: invalid uncompressed length");
    }
    return "";
  }

  return doUncompressString(data, uncompressedLength);
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

std::vector<std::string> Codec::validPrefixes() const {
  return {};
}

bool Codec::canUncompress(const IOBuf*, uint64_t) const {
  return false;
}

std::string Codec::doCompressString(const StringPiece data) {
  const IOBuf inputBuffer{IOBuf::WRAP_BUFFER, data};
  auto outputBuffer = doCompress(&inputBuffer);
  std::string output;
  output.reserve(outputBuffer->computeChainDataLength());
  for (auto range : *outputBuffer) {
    output.append(reinterpret_cast<const char*>(range.data()), range.size());
  }
  return output;
}

std::string Codec::doUncompressString(
    const StringPiece data,
    uint64_t uncompressedLength) {
  const IOBuf inputBuffer{IOBuf::WRAP_BUFFER, data};
  auto outputBuffer = doUncompress(&inputBuffer, uncompressedLength);
  std::string output;
  output.reserve(outputBuffer->computeChainDataLength());
  for (auto range : *outputBuffer) {
    output.append(reinterpret_cast<const char*>(range.data()), range.size());
  }
  return output;
}

namespace {

/**
 * No compression
 */
class NoCompressionCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit NoCompressionCodec(int level, CodecType type);

 private:
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;
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

namespace {
/**
 * Reads sizeof(T) bytes, and returns false if not enough bytes are available.
 * Returns true if the first n bytes are equal to prefix when interpreted as
 * a little endian T.
 */
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
dataStartsWithLE(const IOBuf* data, T prefix, uint64_t n = sizeof(T)) {
  DCHECK_GT(n, 0);
  DCHECK_LE(n, sizeof(T));
  T value;
  Cursor cursor{data};
  if (!cursor.tryReadLE(value)) {
    return false;
  }
  const T mask = n == sizeof(T) ? T(-1) : (T(1) << (8 * n)) - 1;
  return prefix == (value & mask);
}

template <typename T>
typename std::enable_if<std::is_arithmetic<T>::value, std::string>::type
prefixToStringLE(T prefix, uint64_t n = sizeof(T)) {
  DCHECK_GT(n, 0);
  DCHECK_LE(n, sizeof(T));
  prefix = Endian::little(prefix);
  std::string result;
  result.resize(n);
  memcpy(&result[0], &prefix, n);
  return result;
}

static uint64_t computeBufferLength(
    uint64_t const compressedLength,
    uint64_t const blockSize) {
  uint64_t constexpr kMaxBufferLength = uint64_t(4) << 20; // 4 MiB
  uint64_t const goodBufferSize = 4 * std::max(blockSize, compressedLength);
  return std::min(goodBufferSize, kMaxBufferLength);
}
} // namespace

#if FOLLY_HAVE_LIBLZ4

/**
 * LZ4 compression
 */
class LZ4Codec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit LZ4Codec(int level, CodecType type);

 private:
  bool doNeedsUncompressedLength() const override;
  uint64_t doMaxUncompressedLength() const override;

  bool encodeSize() const { return type() == CodecType::LZ4_VARINT_SIZE; }

  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

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
  IOBuf clone;
  if (data->isChained()) {
    // LZ4 doesn't support streaming, so we have to coalesce
    clone = data->cloneCoalescedAsValue();
    data = &clone;
  }

  uint32_t extraSize = encodeSize() ? kMaxVarintLength64 : 0;
  auto out = IOBuf::create(extraSize + LZ4_compressBound(data->length()));
  if (encodeSize()) {
    encodeVarintToIOBuf(data->length(), out.get());
  }

  int n;
  auto input = reinterpret_cast<const char*>(data->data());
  auto output = reinterpret_cast<char*>(out->writableTail());
  const auto inputLength = data->length();
#if LZ4_VERSION_NUMBER >= 10700
  if (highCompression_) {
    n = LZ4_compress_HC(input, output, inputLength, out->tailroom(), 0);
  } else {
    n = LZ4_compress_default(input, output, inputLength, out->tailroom());
  }
#else
  if (highCompression_) {
    n = LZ4_compressHC(input, output, inputLength);
  } else {
    n = LZ4_compress(input, output, inputLength);
  }
#endif

  CHECK_GE(n, 0);
  CHECK_LE(n, out->capacity());

  out->append(n);
  return out;
}

std::unique_ptr<IOBuf> LZ4Codec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  IOBuf clone;
  if (data->isChained()) {
    // LZ4 doesn't support streaming, so we have to coalesce
    clone = data->cloneCoalescedAsValue();
    data = &clone;
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

  auto sp = StringPiece{cursor.peekBytes()};
  auto out = IOBuf::create(actualUncompressedLength);
  int n = LZ4_decompress_safe(
      sp.data(),
      reinterpret_cast<char*>(out->writableTail()),
      sp.size(),
      actualUncompressedLength);

  if (n < 0 || uint64_t(n) != actualUncompressedLength) {
    throw std::runtime_error(to<std::string>(
        "LZ4 decompression returned invalid value ", n));
  }
  out->append(actualUncompressedLength);
  return out;
}

#if LZ4_VERSION_NUMBER >= 10301

class LZ4FrameCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit LZ4FrameCodec(int level, CodecType type);
  ~LZ4FrameCodec();

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(const IOBuf* data, uint64_t uncompressedLength)
      const override;

 private:
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

  // Reset the dctx_ if it is dirty or null.
  void resetDCtx();

  int level_;
  LZ4F_decompressionContext_t dctx_{nullptr};
  bool dirty_{false};
};

/* static */ std::unique_ptr<Codec> LZ4FrameCodec::create(
    int level,
    CodecType type) {
  return make_unique<LZ4FrameCodec>(level, type);
}

static constexpr uint32_t kLZ4FrameMagicLE = 0x184D2204;

std::vector<std::string> LZ4FrameCodec::validPrefixes() const {
  return {prefixToStringLE(kLZ4FrameMagicLE)};
}

bool LZ4FrameCodec::canUncompress(const IOBuf* data, uint64_t) const {
  return dataStartsWithLE(data, kLZ4FrameMagicLE);
}

static size_t lz4FrameThrowOnError(size_t code) {
  if (LZ4F_isError(code)) {
    throw std::runtime_error(
        to<std::string>("LZ4Frame error: ", LZ4F_getErrorName(code)));
  }
  return code;
}

void LZ4FrameCodec::resetDCtx() {
  if (dctx_ && !dirty_) {
    return;
  }
  if (dctx_) {
    LZ4F_freeDecompressionContext(dctx_);
  }
  lz4FrameThrowOnError(LZ4F_createDecompressionContext(&dctx_, 100));
  dirty_ = false;
}

LZ4FrameCodec::LZ4FrameCodec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::LZ4_FRAME);
  switch (level) {
    case COMPRESSION_LEVEL_FASTEST:
    case COMPRESSION_LEVEL_DEFAULT:
      level_ = 0;
      break;
    case COMPRESSION_LEVEL_BEST:
      level_ = 16;
      break;
    default:
      level_ = level;
      break;
  }
}

LZ4FrameCodec::~LZ4FrameCodec() {
  if (dctx_) {
    LZ4F_freeDecompressionContext(dctx_);
  }
}

std::unique_ptr<IOBuf> LZ4FrameCodec::doCompress(const IOBuf* data) {
  // LZ4 Frame compression doesn't support streaming so we have to coalesce
  IOBuf clone;
  if (data->isChained()) {
    clone = data->cloneCoalescedAsValue();
    data = &clone;
  }
  // Set preferences
  const auto uncompressedLength = data->length();
  LZ4F_preferences_t prefs{};
  prefs.compressionLevel = level_;
  prefs.frameInfo.contentSize = uncompressedLength;
  // Compress
  auto buf = IOBuf::create(LZ4F_compressFrameBound(uncompressedLength, &prefs));
  const size_t written = lz4FrameThrowOnError(LZ4F_compressFrame(
      buf->writableTail(),
      buf->tailroom(),
      data->data(),
      data->length(),
      &prefs));
  buf->append(written);
  return buf;
}

std::unique_ptr<IOBuf> LZ4FrameCodec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  // Reset the dctx if any errors have occurred
  resetDCtx();
  // Coalesce the data
  ByteRange in = *data->begin();
  IOBuf clone;
  if (data->isChained()) {
    clone = data->cloneCoalescedAsValue();
    in = clone.coalesce();
  }
  data = nullptr;
  // Select decompression options
  LZ4F_decompressOptions_t options;
  options.stableDst = 1;
  // Select blockSize and growthSize for the IOBufQueue
  IOBufQueue queue(IOBufQueue::cacheChainLength());
  auto blockSize = uint64_t{64} << 10;
  auto growthSize = uint64_t{4} << 20;
  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH) {
    // Allocate uncompressedLength in one chunk (up to 64 MB)
    const auto allocateSize = std::min(uncompressedLength, uint64_t{64} << 20);
    queue.preallocate(allocateSize, allocateSize);
    blockSize = std::min(uncompressedLength, blockSize);
    growthSize = std::min(uncompressedLength, growthSize);
  } else {
    // Reduce growthSize for small data
    const auto guessUncompressedLen =
        4 * std::max<uint64_t>(blockSize, in.size());
    growthSize = std::min(guessUncompressedLen, growthSize);
  }
  // Once LZ4_decompress() is called, the dctx_ cannot be reused until it
  // returns 0
  dirty_ = true;
  // Decompress until the frame is over
  size_t code = 0;
  do {
    // Allocate enough space to decompress at least a block
    void* out;
    size_t outSize;
    std::tie(out, outSize) = queue.preallocate(blockSize, growthSize);
    // Decompress
    size_t inSize = in.size();
    code = lz4FrameThrowOnError(
        LZ4F_decompress(dctx_, out, &outSize, in.data(), &inSize, &options));
    if (in.empty() && outSize == 0 && code != 0) {
      // We passed no input, no output was produced, and the frame isn't over
      // No more forward progress is possible
      throw std::runtime_error("LZ4Frame error: Incomplete frame");
    }
    in.uncheckedAdvance(inSize);
    queue.postallocate(outSize);
  } while (code != 0);
  // At this point the decompression context can be reused
  dirty_ = false;
  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
      queue.chainLength() != uncompressedLength) {
    throw std::runtime_error("LZ4Frame error: Invalid uncompressedLength");
  }
  return queue.move();
}

#endif // LZ4_VERSION_NUMBER >= 10301
#endif // FOLLY_HAVE_LIBLZ4

#if FOLLY_HAVE_LIBSNAPPY

/**
 * Snappy compression
 */

/**
 * Implementation of snappy::Source that reads from a IOBuf chain.
 */
class IOBufSnappySource final : public snappy::Source {
 public:
  explicit IOBufSnappySource(const IOBuf* data);
  size_t Available() const override;
  const char* Peek(size_t* len) override;
  void Skip(size_t n) override;
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
  auto sp = StringPiece{cursor_.peekBytes()};
  *len = sp.size();
  return sp.data();
}

void IOBufSnappySource::Skip(size_t n) {
  CHECK_LE(n, available_);
  cursor_.skip(n);
  available_ -= n;
}

class SnappyCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit SnappyCodec(int level, CodecType type);

 private:
  uint64_t doMaxUncompressedLength() const override;
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;
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
class ZlibCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit ZlibCodec(int level, CodecType type);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(const IOBuf* data, uint64_t uncompressedLength)
      const override;

 private:
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

  std::unique_ptr<IOBuf> addOutputBuffer(z_stream* stream, uint32_t length);
  bool doInflate(z_stream* stream, IOBuf* head, uint32_t bufferLength);

  int level_;
};

static constexpr uint16_t kGZIPMagicLE = 0x8B1F;

std::vector<std::string> ZlibCodec::validPrefixes() const {
  if (type() == CodecType::ZLIB) {
    // Zlib streams start with a 2 byte header.
    //
    //   0   1
    // +---+---+
    // |CMF|FLG|
    // +---+---+
    //
    // We won't restrict the values of any sub-fields except as described below.
    //
    // The lowest 4 bits of CMF is the compression method (CM).
    // CM == 0x8 is the deflate compression method, which is currently the only
    // supported compression method, so any valid prefix must have CM == 0x8.
    //
    // The lowest 5 bits of FLG is FCHECK.
    // FCHECK must be such that the two header bytes are a multiple of 31 when
    // interpreted as a big endian 16-bit number.
    std::vector<std::string> result;
    // 16 values for the first byte, 8 values for the second byte.
    // There are also 4 combinations where both 0x00 and 0x1F work as FCHECK.
    result.reserve(132);
    // Select all values for the CMF byte that use the deflate algorithm 0x8.
    for (uint32_t first = 0x0800; first <= 0xF800; first += 0x1000) {
      // Select all values for the FLG, but leave FCHECK as 0 since it's fixed.
      for (uint32_t second = 0x00; second <= 0xE0; second += 0x20) {
        uint16_t prefix = first | second;
        // Compute FCHECK.
        prefix += 31 - (prefix % 31);
        result.push_back(prefixToStringLE(Endian::big(prefix)));
        // zlib won't produce this, but it is a valid prefix.
        if ((prefix & 0x1F) == 31) {
          prefix -= 31;
          result.push_back(prefixToStringLE(Endian::big(prefix)));
        }
      }
    }
    return result;
  } else {
    // The gzip frame starts with 2 magic bytes.
    return {prefixToStringLE(kGZIPMagicLE)};
  }
}

bool ZlibCodec::canUncompress(const IOBuf* data, uint64_t) const {
  if (type() == CodecType::ZLIB) {
    uint16_t value;
    Cursor cursor{data};
    if (!cursor.tryReadBE(value)) {
      return false;
    }
    // zlib compressed if using deflate and is a multiple of 31.
    return (value & 0x0F00) == 0x0800 && value % 31 == 0;
  } else {
    return dataStartsWithLE(data, kGZIPMagicLE);
  }
}

std::unique_ptr<Codec> ZlibCodec::create(int level, CodecType type) {
  return make_unique<ZlibCodec>(level, type);
}

ZlibCodec::ZlibCodec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::ZLIB || type == CodecType::GZIP);
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
  buf->append(buf->capacity());

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

  // Using deflateInit2() to support gzip.  "The windowBits parameter is the
  // base two logarithm of the maximum window size (...) The default value is
  // 15 (...) Add 16 to windowBits to write a simple gzip header and trailer
  // around the compressed data instead of a zlib wrapper. The gzip header
  // will have no file name, no extra data, no comment, no modification time
  // (set to zero), no header crc, and the operating system will be set to 255
  // (unknown)."
  int windowBits = 15 + (type() == CodecType::GZIP ? 16 : 0);
  // All other parameters (method, memLevel, strategy) get default values from
  // the zlib manual.
  int rc = deflateInit2(&stream,
                        level_,
                        Z_DEFLATED,
                        windowBits,
                        /* memLevel */ 8,
                        Z_DEFAULT_STRATEGY);
  if (rc != Z_OK) {
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: deflateInit error: ", rc, ": ", stream.msg));
  }

  stream.next_in = stream.next_out = nullptr;
  stream.avail_in = stream.avail_out = 0;
  stream.total_in = stream.total_out = 0;

  bool success = false;

  SCOPE_EXIT {
    rc = deflateEnd(&stream);
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

  // "The windowBits parameter is the base two logarithm of the maximum window
  // size (...) The default value is 15 (...) add 16 to decode only the gzip
  // format (the zlib format will return a Z_DATA_ERROR)."
  int windowBits = 15 + (type() == CodecType::GZIP ? 16 : 0);
  int rc = inflateInit2(&stream, windowBits);
  if (rc != Z_OK) {
    throw std::runtime_error(to<std::string>(
        "ZlibCodec: inflateInit error: ", rc, ": ", stream.msg));
  }

  stream.next_in = stream.next_out = nullptr;
  stream.avail_in = stream.avail_out = 0;
  stream.total_in = stream.total_out = 0;

  bool success = false;

  SCOPE_EXIT {
    rc = inflateEnd(&stream);
    // If we're here because of an exception, it's okay if some data
    // got dropped.
    CHECK(rc == Z_OK || (!success && rc == Z_DATA_ERROR))
      << rc << ": " << stream.msg;
  };

  // Max 64MiB in one go
  constexpr uint64_t maxSingleStepLength = uint64_t(64) << 20; // 64MiB
  constexpr uint64_t kBlockSize = uint64_t(32) << 10; // 32 KiB
  const uint64_t defaultBufferLength =
      computeBufferLength(data->computeChainDataLength(), kBlockSize);

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
class LZMA2Codec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit LZMA2Codec(int level, CodecType type);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(const IOBuf* data, uint64_t uncompressedLength)
      const override;

 private:
  bool doNeedsUncompressedLength() const override;
  uint64_t doMaxUncompressedLength() const override;

  bool encodeSize() const { return type() == CodecType::LZMA2_VARINT_SIZE; }

  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

  std::unique_ptr<IOBuf> addOutputBuffer(lzma_stream* stream, size_t length);
  bool doInflate(lzma_stream* stream, IOBuf* head, size_t bufferLength);

  int level_;
};

static constexpr uint64_t kLZMA2MagicLE = 0x005A587A37FD;
static constexpr unsigned kLZMA2MagicBytes = 6;

std::vector<std::string> LZMA2Codec::validPrefixes() const {
  if (type() == CodecType::LZMA2_VARINT_SIZE) {
    return {};
  }
  return {prefixToStringLE(kLZMA2MagicLE, kLZMA2MagicBytes)};
}

bool LZMA2Codec::canUncompress(const IOBuf* data, uint64_t) const {
  if (type() == CodecType::LZMA2_VARINT_SIZE) {
    return false;
  }
  // Returns false for all inputs less than 8 bytes.
  // This is okay, because no valid LZMA2 streams are less than 8 bytes.
  return dataStartsWithLE(data, kLZMA2MagicLE, kLZMA2MagicBytes);
}

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
  return false;
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
  buf->append(buf->capacity());

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
  constexpr uint32_t maxSingleStepLength = uint32_t(64) << 20; // 64MiB
  constexpr uint32_t defaultBufferLength = uint32_t(256) << 10; // 256 KiB

  folly::io::Cursor cursor(data);
  if (encodeSize()) {
    const uint64_t actualUncompressedLength = decodeVarintFromCursor(cursor);
    if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength != actualUncompressedLength) {
      throw std::runtime_error("LZMA2Codec: invalid uncompressed length");
    }
    uncompressedLength = actualUncompressedLength;
  }

  auto out = addOutputBuffer(
      &stream,
      ((uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength <= maxSingleStepLength)
           ? uncompressedLength
           : defaultBufferLength));

  bool streamEnd = false;
  auto buf = cursor.peekBytes();
  while (!buf.empty()) {
    stream.next_in = const_cast<uint8_t*>(buf.data());
    stream.avail_in = buf.size();

    while (stream.avail_in != 0) {
      if (streamEnd) {
        throw std::runtime_error(to<std::string>(
            "LZMA2Codec: junk after end of data"));
      }

      streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
    }

    cursor.skip(buf.size());
    buf = cursor.peekBytes();
  }

  while (!streamEnd) {
    streamEnd = doInflate(&stream, out.get(), defaultBufferLength);
  }

  out->prev()->trimEnd(stream.avail_out);

  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
      uncompressedLength != stream.total_out) {
    throw std::runtime_error(
        to<std::string>("LZMA2Codec: invalid uncompressed length"));
  }

  return out;
}

#endif  // FOLLY_HAVE_LIBLZMA

#ifdef FOLLY_HAVE_LIBZSTD

/**
 * ZSTD compression
 */
class ZSTDCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType);
  explicit ZSTDCodec(int level, CodecType type);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(const IOBuf* data, uint64_t uncompressedLength)
      const override;

 private:
  bool doNeedsUncompressedLength() const override;
  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

  int level_;
};

static constexpr uint32_t kZSTDMagicLE = 0xFD2FB528;

std::vector<std::string> ZSTDCodec::validPrefixes() const {
  return {prefixToStringLE(kZSTDMagicLE)};
}

bool ZSTDCodec::canUncompress(const IOBuf* data, uint64_t) const {
  return dataStartsWithLE(data, kZSTDMagicLE);
}

std::unique_ptr<Codec> ZSTDCodec::create(int level, CodecType type) {
  return make_unique<ZSTDCodec>(level, type);
}

ZSTDCodec::ZSTDCodec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::ZSTD);
  switch (level) {
    case COMPRESSION_LEVEL_FASTEST:
      level = 1;
      break;
    case COMPRESSION_LEVEL_DEFAULT:
      level = 1;
      break;
    case COMPRESSION_LEVEL_BEST:
      level = 19;
      break;
  }
  if (level < 1 || level > ZSTD_maxCLevel()) {
    throw std::invalid_argument(
        to<std::string>("ZSTD: invalid level: ", level));
  }
  level_ = level;
}

bool ZSTDCodec::doNeedsUncompressedLength() const {
  return false;
}

void zstdThrowIfError(size_t rc) {
  if (!ZSTD_isError(rc)) {
    return;
  }
  throw std::runtime_error(
      to<std::string>("ZSTD returned an error: ", ZSTD_getErrorName(rc)));
}

std::unique_ptr<IOBuf> ZSTDCodec::doCompress(const IOBuf* data) {
  // Support earlier versions of the codec (working with a single IOBuf,
  // and using ZSTD_decompress which requires ZSTD frame to contain size,
  // which isn't populated by streaming API).
  if (!data->isChained()) {
    auto out = IOBuf::createCombined(ZSTD_compressBound(data->length()));
    const auto rc = ZSTD_compress(
        out->writableData(),
        out->capacity(),
        data->data(),
        data->length(),
        level_);
    zstdThrowIfError(rc);
    out->append(rc);
    return out;
  }

  auto zcs = ZSTD_createCStream();
  SCOPE_EXIT {
    ZSTD_freeCStream(zcs);
  };

  auto rc = ZSTD_initCStream(zcs, level_);
  zstdThrowIfError(rc);

  Cursor cursor(data);
  auto result = IOBuf::createCombined(ZSTD_compressBound(cursor.totalLength()));

  ZSTD_outBuffer out;
  out.dst = result->writableTail();
  out.size = result->capacity();
  out.pos = 0;

  for (auto buffer = cursor.peekBytes(); !buffer.empty();) {
    ZSTD_inBuffer in;
    in.src = buffer.data();
    in.size = buffer.size();
    for (in.pos = 0; in.pos != in.size;) {
      rc = ZSTD_compressStream(zcs, &out, &in);
      zstdThrowIfError(rc);
    }
    cursor.skip(in.size);
    buffer = cursor.peekBytes();
  }

  rc = ZSTD_endStream(zcs, &out);
  zstdThrowIfError(rc);
  CHECK_EQ(rc, 0);

  result->append(out.pos);
  return result;
}

static std::unique_ptr<IOBuf> zstdUncompressBuffer(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  // Check preconditions
  DCHECK(!data->isChained());
  DCHECK(uncompressedLength != Codec::UNKNOWN_UNCOMPRESSED_LENGTH);

  auto uncompressed = IOBuf::create(uncompressedLength);
  const auto decompressedSize = ZSTD_decompress(
      uncompressed->writableTail(),
      uncompressed->tailroom(),
      data->data(),
      data->length());
  zstdThrowIfError(decompressedSize);
  if (decompressedSize != uncompressedLength) {
    throw std::runtime_error("ZSTD: invalid uncompressed length");
  }
  uncompressed->append(decompressedSize);
  return uncompressed;
}

static std::unique_ptr<IOBuf> zstdUncompressStream(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  auto zds = ZSTD_createDStream();
  SCOPE_EXIT {
    ZSTD_freeDStream(zds);
  };

  auto rc = ZSTD_initDStream(zds);
  zstdThrowIfError(rc);

  ZSTD_outBuffer out{};
  ZSTD_inBuffer in{};

  auto outputSize = ZSTD_DStreamOutSize();
  if (uncompressedLength != Codec::UNKNOWN_UNCOMPRESSED_LENGTH) {
    outputSize = uncompressedLength;
  }

  IOBufQueue queue(IOBufQueue::cacheChainLength());

  Cursor cursor(data);
  for (rc = 0;;) {
    if (in.pos == in.size) {
      auto buffer = cursor.peekBytes();
      in.src = buffer.data();
      in.size = buffer.size();
      in.pos = 0;
      cursor.skip(in.size);
      if (rc > 1 && in.size == 0) {
        throw std::runtime_error(to<std::string>("ZSTD: incomplete input"));
      }
    }
    if (out.pos == out.size) {
      if (out.pos != 0) {
        queue.postallocate(out.pos);
      }
      auto buffer = queue.preallocate(outputSize, outputSize);
      out.dst = buffer.first;
      out.size = buffer.second;
      out.pos = 0;
      outputSize = ZSTD_DStreamOutSize();
    }
    rc = ZSTD_decompressStream(zds, &out, &in);
    zstdThrowIfError(rc);
    if (rc == 0) {
      break;
    }
  }
  if (out.pos != 0) {
    queue.postallocate(out.pos);
  }
  if (in.pos != in.size || !cursor.isAtEnd()) {
    throw std::runtime_error("ZSTD: junk after end of data");
  }
  if (uncompressedLength != Codec::UNKNOWN_UNCOMPRESSED_LENGTH &&
      queue.chainLength() != uncompressedLength) {
    throw std::runtime_error("ZSTD: invalid uncompressed length");
  }

  return queue.move();
}

std::unique_ptr<IOBuf> ZSTDCodec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  {
    // Read decompressed size from frame if available in first IOBuf.
    const auto decompressedSize =
        ZSTD_getDecompressedSize(data->data(), data->length());
    if (decompressedSize != 0) {
      if (uncompressedLength != Codec::UNKNOWN_UNCOMPRESSED_LENGTH &&
          uncompressedLength != decompressedSize) {
        throw std::runtime_error("ZSTD: invalid uncompressed length");
      }
      uncompressedLength = decompressedSize;
    }
  }
  // Faster to decompress using ZSTD_decompress() if we can.
  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH && !data->isChained()) {
    return zstdUncompressBuffer(data, uncompressedLength);
  }
  // Fall back to slower streaming decompression.
  return zstdUncompressStream(data, uncompressedLength);
}

#endif  // FOLLY_HAVE_LIBZSTD

#if FOLLY_HAVE_LIBBZ2

class Bzip2Codec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(int level, CodecType type);
  explicit Bzip2Codec(int level, CodecType type);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(IOBuf const* data, uint64_t uncompressedLength)
      const override;

 private:
  std::unique_ptr<IOBuf> doCompress(IOBuf const* data) override;
  std::unique_ptr<IOBuf> doUncompress(
      IOBuf const* data,
      uint64_t uncompressedLength) override;

  int level_;
};

/* static */ std::unique_ptr<Codec> Bzip2Codec::create(
    int level,
    CodecType type) {
  return make_unique<Bzip2Codec>(level, type);
}

Bzip2Codec::Bzip2Codec(int level, CodecType type) : Codec(type) {
  DCHECK(type == CodecType::BZIP2);
  switch (level) {
    case COMPRESSION_LEVEL_FASTEST:
      level = 1;
      break;
    case COMPRESSION_LEVEL_DEFAULT:
      level = 9;
      break;
    case COMPRESSION_LEVEL_BEST:
      level = 9;
      break;
  }
  if (level < 1 || level > 9) {
    throw std::invalid_argument(
        to<std::string>("Bzip2: invalid level: ", level));
  }
  level_ = level;
}

static uint32_t constexpr kBzip2MagicLE = 0x685a42;
static uint64_t constexpr kBzip2MagicBytes = 3;

std::vector<std::string> Bzip2Codec::validPrefixes() const {
  return {prefixToStringLE(kBzip2MagicLE, kBzip2MagicBytes)};
}

bool Bzip2Codec::canUncompress(IOBuf const* data, uint64_t) const {
  return dataStartsWithLE(data, kBzip2MagicLE, kBzip2MagicBytes);
}

static bz_stream createBzStream() {
  bz_stream stream;
  stream.bzalloc = nullptr;
  stream.bzfree = nullptr;
  stream.opaque = nullptr;
  stream.next_in = stream.next_out = nullptr;
  stream.avail_in = stream.avail_out = 0;
  return stream;
}

// Throws on error condition, otherwise returns the code.
static int bzCheck(int const rc) {
  switch (rc) {
    case BZ_OK:
    case BZ_RUN_OK:
    case BZ_FLUSH_OK:
    case BZ_FINISH_OK:
    case BZ_STREAM_END:
      return rc;
    default:
      throw std::runtime_error(to<std::string>("Bzip2 error: ", rc));
  }
}

static uint64_t bzCompressBound(uint64_t const uncompressedLength) {
  // http://www.bzip.org/1.0.5/bzip2-manual-1.0.5.html#bzbufftobuffcompress
  //   To guarantee that the compressed data will fit in its buffer, allocate an
  //   output buffer of size 1% larger than the uncompressed data, plus six
  //   hundred extra bytes.
  return uncompressedLength + uncompressedLength / 100 + 600;
}

static std::unique_ptr<IOBuf> addOutputBuffer(
    bz_stream* stream,
    uint64_t const bufferLength) {
  DCHECK_LE(bufferLength, std::numeric_limits<unsigned>::max());
  DCHECK_EQ(stream->avail_out, 0);

  auto buf = IOBuf::create(bufferLength);
  buf->append(buf->capacity());

  stream->next_out = reinterpret_cast<char*>(buf->writableData());
  stream->avail_out = buf->length();

  return buf;
}

std::unique_ptr<IOBuf> Bzip2Codec::doCompress(IOBuf const* data) {
  bz_stream stream = createBzStream();
  bzCheck(BZ2_bzCompressInit(&stream, level_, 0, 0));
  SCOPE_EXIT {
    bzCheck(BZ2_bzCompressEnd(&stream));
  };

  uint64_t const uncompressedLength = data->computeChainDataLength();
  uint64_t const maxCompressedLength = bzCompressBound(uncompressedLength);
  uint64_t constexpr kMaxSingleStepLength = uint64_t(64) << 20; // 64 MiB
  uint64_t constexpr kDefaultBufferLength = uint64_t(4) << 20;

  auto out = addOutputBuffer(
      &stream,
      maxCompressedLength <= kMaxSingleStepLength ? maxCompressedLength
                                                  : kDefaultBufferLength);

  for (auto range : *data) {
    while (!range.empty()) {
      auto const inSize = std::min<size_t>(range.size(), kMaxSingleStepLength);
      stream.next_in =
          const_cast<char*>(reinterpret_cast<char const*>(range.data()));
      stream.avail_in = inSize;

      if (stream.avail_out == 0) {
        out->prependChain(addOutputBuffer(&stream, kDefaultBufferLength));
      }

      bzCheck(BZ2_bzCompress(&stream, BZ_RUN));
      range.uncheckedAdvance(inSize - stream.avail_in);
    }
  }
  do {
    if (stream.avail_out == 0) {
      out->prependChain(addOutputBuffer(&stream, kDefaultBufferLength));
    }
  } while (bzCheck(BZ2_bzCompress(&stream, BZ_FINISH)) != BZ_STREAM_END);

  out->prev()->trimEnd(stream.avail_out);

  return out;
}

std::unique_ptr<IOBuf> Bzip2Codec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  bz_stream stream = createBzStream();
  bzCheck(BZ2_bzDecompressInit(&stream, 0, 0));
  SCOPE_EXIT {
    bzCheck(BZ2_bzDecompressEnd(&stream));
  };

  uint64_t constexpr kMaxSingleStepLength = uint64_t(64) << 20; // 64 MiB
  uint64_t const kBlockSize = uint64_t(100) << 10; // 100 KiB
  uint64_t const kDefaultBufferLength =
      computeBufferLength(data->computeChainDataLength(), kBlockSize);

  auto out = addOutputBuffer(
      &stream,
      ((uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
        uncompressedLength <= kMaxSingleStepLength)
           ? uncompressedLength
           : kDefaultBufferLength));

  int rc = BZ_OK;
  for (auto range : *data) {
    while (!range.empty()) {
      auto const inSize = std::min<size_t>(range.size(), kMaxSingleStepLength);
      stream.next_in =
          const_cast<char*>(reinterpret_cast<char const*>(range.data()));
      stream.avail_in = inSize;

      if (stream.avail_out == 0) {
        out->prependChain(addOutputBuffer(&stream, kDefaultBufferLength));
      }

      rc = bzCheck(BZ2_bzDecompress(&stream));
      range.uncheckedAdvance(inSize - stream.avail_in);
    }
  }
  while (rc != BZ_STREAM_END) {
    if (stream.avail_out == 0) {
      out->prependChain(addOutputBuffer(&stream, kDefaultBufferLength));
    }

    rc = bzCheck(BZ2_bzDecompress(&stream));
  }

  out->prev()->trimEnd(stream.avail_out);

  uint64_t const totalOut =
      (uint64_t(stream.total_out_hi32) << 32) + stream.total_out_lo32;
  if (uncompressedLength != UNKNOWN_UNCOMPRESSED_LENGTH &&
      uncompressedLength != totalOut) {
    throw std::runtime_error("Bzip2 error: Invalid uncompressed length");
  }

  return out;
}

#endif // FOLLY_HAVE_LIBBZ2

/**
 * Automatic decompression
 */
class AutomaticCodec final : public Codec {
 public:
  static std::unique_ptr<Codec> create(
      std::vector<std::unique_ptr<Codec>> customCodecs);
  explicit AutomaticCodec(std::vector<std::unique_ptr<Codec>> customCodecs);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(const IOBuf* data, uint64_t uncompressedLength)
      const override;

 private:
  bool doNeedsUncompressedLength() const override;
  uint64_t doMaxUncompressedLength() const override;

  std::unique_ptr<IOBuf> doCompress(const IOBuf*) override {
    throw std::runtime_error("AutomaticCodec error: compress() not supported.");
  }
  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override;

  void addCodecIfSupported(CodecType type);

  // Throws iff the codecs aren't compatible (very slow)
  void checkCompatibleCodecs() const;

  std::vector<std::unique_ptr<Codec>> codecs_;
  bool needsUncompressedLength_;
  uint64_t maxUncompressedLength_;
};

std::vector<std::string> AutomaticCodec::validPrefixes() const {
  std::unordered_set<std::string> prefixes;
  for (const auto& codec : codecs_) {
    const auto codecPrefixes = codec->validPrefixes();
    prefixes.insert(codecPrefixes.begin(), codecPrefixes.end());
  }
  return std::vector<std::string>{prefixes.begin(), prefixes.end()};
}

bool AutomaticCodec::canUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) const {
  return std::any_of(
      codecs_.begin(),
      codecs_.end(),
      [data, uncompressedLength](std::unique_ptr<Codec> const& codec) {
        return codec->canUncompress(data, uncompressedLength);
      });
}

void AutomaticCodec::addCodecIfSupported(CodecType type) {
  const bool present = std::any_of(
      codecs_.begin(),
      codecs_.end(),
      [&type](std::unique_ptr<Codec> const& codec) {
        return codec->type() == type;
      });
  if (hasCodec(type) && !present) {
    codecs_.push_back(getCodec(type));
  }
}

/* static */ std::unique_ptr<Codec> AutomaticCodec::create(
    std::vector<std::unique_ptr<Codec>> customCodecs) {
  return make_unique<AutomaticCodec>(std::move(customCodecs));
}

AutomaticCodec::AutomaticCodec(std::vector<std::unique_ptr<Codec>> customCodecs)
    : Codec(CodecType::USER_DEFINED), codecs_(std::move(customCodecs)) {
  // Fastest -> slowest
  addCodecIfSupported(CodecType::LZ4_FRAME);
  addCodecIfSupported(CodecType::ZSTD);
  addCodecIfSupported(CodecType::ZLIB);
  addCodecIfSupported(CodecType::GZIP);
  addCodecIfSupported(CodecType::LZMA2);
  addCodecIfSupported(CodecType::BZIP2);
  if (kIsDebug) {
    checkCompatibleCodecs();
  }
  // Check that none of the codes are are null
  DCHECK(std::none_of(
      codecs_.begin(), codecs_.end(), [](std::unique_ptr<Codec> const& codec) {
        return codec == nullptr;
      }));

  needsUncompressedLength_ = std::any_of(
      codecs_.begin(), codecs_.end(), [](std::unique_ptr<Codec> const& codec) {
        return codec->needsUncompressedLength();
      });

  const auto it = std::max_element(
      codecs_.begin(),
      codecs_.end(),
      [](std::unique_ptr<Codec> const& lhs, std::unique_ptr<Codec> const& rhs) {
        return lhs->maxUncompressedLength() < rhs->maxUncompressedLength();
      });
  DCHECK(it != codecs_.end());
  maxUncompressedLength_ = (*it)->maxUncompressedLength();
}

void AutomaticCodec::checkCompatibleCodecs() const {
  // Keep track of all the possible headers.
  std::unordered_set<std::string> headers;
  // The empty header is not allowed.
  headers.insert("");
  // Step 1:
  // Construct a set of headers and check that none of the headers occur twice.
  // Eliminate edge cases.
  for (auto&& codec : codecs_) {
    const auto codecHeaders = codec->validPrefixes();
    // Codecs without any valid headers are not allowed.
    if (codecHeaders.empty()) {
      throw std::invalid_argument{
          "AutomaticCodec: validPrefixes() must not be empty."};
    }
    // Insert all the headers for the current codec.
    const size_t beforeSize = headers.size();
    headers.insert(codecHeaders.begin(), codecHeaders.end());
    // Codecs are not compatible if any header occurred twice.
    if (beforeSize + codecHeaders.size() != headers.size()) {
      throw std::invalid_argument{
          "AutomaticCodec: Two valid prefixes collide."};
    }
  }
  // Step 2:
  // Check if any strict non-empty prefix of any header is a header.
  for (const auto& header : headers) {
    for (size_t i = 1; i < header.size(); ++i) {
      if (headers.count(header.substr(0, i))) {
        throw std::invalid_argument{
            "AutomaticCodec: One valid prefix is a prefix of another valid "
            "prefix."};
      }
    }
  }
}

bool AutomaticCodec::doNeedsUncompressedLength() const {
  return needsUncompressedLength_;
}

uint64_t AutomaticCodec::doMaxUncompressedLength() const {
  return maxUncompressedLength_;
}

std::unique_ptr<IOBuf> AutomaticCodec::doUncompress(
    const IOBuf* data,
    uint64_t uncompressedLength) {
  for (auto&& codec : codecs_) {
    if (codec->canUncompress(data, uncompressedLength)) {
      return codec->uncompress(data, uncompressedLength);
    }
  }
  throw std::runtime_error("AutomaticCodec error: Unknown compressed data");
}

}  // namespace

typedef std::unique_ptr<Codec> (*CodecFactory)(int, CodecType);
static constexpr CodecFactory
    codecFactories[static_cast<size_t>(CodecType::NUM_CODEC_TYPES)] = {
        nullptr, // USER_DEFINED
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

#if FOLLY_HAVE_LIBZSTD
        ZSTDCodec::create,
#else
        nullptr,
#endif

#if FOLLY_HAVE_LIBZ
        ZlibCodec::create,
#else
        nullptr,
#endif

#if (FOLLY_HAVE_LIBLZ4 && LZ4_VERSION_NUMBER >= 10301)
        LZ4FrameCodec::create,
#else
        nullptr,
#endif

#if FOLLY_HAVE_LIBBZ2
        Bzip2Codec::create,
#else
        nullptr
#endif
};

bool hasCodec(CodecType type) {
  size_t idx = static_cast<size_t>(type);
  if (idx >= static_cast<size_t>(CodecType::NUM_CODEC_TYPES)) {
    throw std::invalid_argument(
        to<std::string>("Compression type ", idx, " invalid"));
  }
  return codecFactories[idx] != nullptr;
}

std::unique_ptr<Codec> getCodec(CodecType type, int level) {
  size_t idx = static_cast<size_t>(type);
  if (idx >= static_cast<size_t>(CodecType::NUM_CODEC_TYPES)) {
    throw std::invalid_argument(
        to<std::string>("Compression type ", idx, " invalid"));
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

std::unique_ptr<Codec> getAutoUncompressionCodec(
    std::vector<std::unique_ptr<Codec>> customCodecs) {
  return AutomaticCodec::create(std::move(customCodecs));
}
}}  // namespaces
