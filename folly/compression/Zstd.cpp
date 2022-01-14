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

#include <folly/compression/Zstd.h>

#if FOLLY_HAVE_LIBZSTD

#include <stdexcept>
#include <string>

#include <zstd.h>

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/compression/CompressionContextPoolSingletons.h>
#include <folly/compression/Utils.h>

static_assert(
    ZSTD_VERSION_NUMBER >= 10400,
    "zstd-1.4.0 is the minimum supported zstd version.");

using folly::io::compression::detail::dataStartsWithLE;
using folly::io::compression::detail::prefixToStringLE;

using namespace folly::compression::contexts;

namespace folly {
namespace io {
namespace zstd {
namespace {

size_t zstdThrowIfError(size_t rc) {
  if (!ZSTD_isError(rc)) {
    return rc;
  }
  throw std::runtime_error(
      to<std::string>("ZSTD returned an error: ", ZSTD_getErrorName(rc)));
}

ZSTD_EndDirective zstdTranslateFlush(StreamCodec::FlushOp flush) {
  switch (flush) {
    case StreamCodec::FlushOp::NONE:
      return ZSTD_e_continue;
    case StreamCodec::FlushOp::FLUSH:
      return ZSTD_e_flush;
    case StreamCodec::FlushOp::END:
      return ZSTD_e_end;
    default:
      throw std::invalid_argument("ZSTDStreamCodec: Invalid flush");
  }
}

class ZSTDStreamCodec final : public StreamCodec {
 public:
  explicit ZSTDStreamCodec(Options options);

  std::vector<std::string> validPrefixes() const override;
  bool canUncompress(
      const IOBuf* data, Optional<uint64_t> uncompressedLength) const override;

 private:
  bool doNeedsUncompressedLength() const override;
  uint64_t doMaxCompressedLength(uint64_t uncompressedLength) const override;
  Optional<uint64_t> doGetUncompressedLength(
      IOBuf const* data, Optional<uint64_t> uncompressedLength) const override;

  void doResetStream() override;
  bool doCompressStream(
      ByteRange& input,
      MutableByteRange& output,
      StreamCodec::FlushOp flushOp) override;
  bool doUncompressStream(
      ByteRange& input,
      MutableByteRange& output,
      StreamCodec::FlushOp flushOp) override;

  void resetCCtx();
  void resetDCtx();

  Options options_;
  ZSTD_CCtx_Pool::Ref cctx_{getNULL_ZSTD_CCtx()};
  ZSTD_DCtx_Pool::Ref dctx_{getNULL_ZSTD_DCtx()};
};

constexpr uint32_t kZSTDMagicLE = 0xFD2FB528;

std::vector<std::string> ZSTDStreamCodec::validPrefixes() const {
  return {prefixToStringLE(kZSTDMagicLE)};
}

bool ZSTDStreamCodec::canUncompress(
    const IOBuf* data, Optional<uint64_t>) const {
  return dataStartsWithLE(data, kZSTDMagicLE);
}

CodecType codecType(Options const& options) {
  int const level = options.level();
  DCHECK_NE(level, 0);
  return level > 0 ? CodecType::ZSTD : CodecType::ZSTD_FAST;
}

ZSTDStreamCodec::ZSTDStreamCodec(Options options)
    : StreamCodec(codecType(options), options.level()),
      options_(std::move(options)) {}

bool ZSTDStreamCodec::doNeedsUncompressedLength() const {
  return false;
}

uint64_t ZSTDStreamCodec::doMaxCompressedLength(
    uint64_t uncompressedLength) const {
  return ZSTD_compressBound(uncompressedLength);
}

Optional<uint64_t> ZSTDStreamCodec::doGetUncompressedLength(
    IOBuf const* data, Optional<uint64_t> uncompressedLength) const {
  // Read decompressed size from frame if available in first IOBuf.
  auto const decompressedSize =
      ZSTD_getFrameContentSize(data->data(), data->length());
  if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN ||
      decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
    return uncompressedLength;
  }
  if (uncompressedLength && *uncompressedLength != decompressedSize) {
    throw std::runtime_error("ZSTD: invalid uncompressed length");
  }
  return decompressedSize;
}

void ZSTDStreamCodec::doResetStream() {
  cctx_.reset(nullptr);
  dctx_.reset(nullptr);
}

void ZSTDStreamCodec::resetCCtx() {
  DCHECK(cctx_ == nullptr);
  cctx_ = getZSTD_CCtx(); // Gives us a clean context
  DCHECK(cctx_ != nullptr);
  zstdThrowIfError(
      ZSTD_CCtx_setParametersUsingCCtxParams(cctx_.get(), options_.params()));
  zstdThrowIfError(ZSTD_CCtx_setPledgedSrcSize(
      cctx_.get(), uncompressedLength().value_or(ZSTD_CONTENTSIZE_UNKNOWN)));
}

bool ZSTDStreamCodec::doCompressStream(
    ByteRange& input, MutableByteRange& output, StreamCodec::FlushOp flushOp) {
  if (cctx_ == nullptr) {
    resetCCtx();
  }
  ZSTD_inBuffer in = {input.data(), input.size(), 0};
  ZSTD_outBuffer out = {output.data(), output.size(), 0};
  FOLLY_SCOPE_EXIT {
    input.uncheckedAdvance(in.pos);
    output.uncheckedAdvance(out.pos);
  };
  size_t const rc = zstdThrowIfError(ZSTD_compressStream2(
      cctx_.get(), &out, &in, zstdTranslateFlush(flushOp)));
  switch (flushOp) {
    case StreamCodec::FlushOp::NONE:
      return false;
    case StreamCodec::FlushOp::FLUSH:
      return rc == 0;
    case StreamCodec::FlushOp::END:
      if (rc == 0) {
        // Surrender our cctx_
        doResetStream();
      }
      return rc == 0;
    default:
      throw std::invalid_argument("ZSTD: invalid FlushOp");
  }
}

void ZSTDStreamCodec::resetDCtx() {
  DCHECK(dctx_ == nullptr);
  dctx_ = getZSTD_DCtx(); // Gives us a clean context
  DCHECK(dctx_ != nullptr);
  if (options_.maxWindowSize() != 0) {
    zstdThrowIfError(
        ZSTD_DCtx_setMaxWindowSize(dctx_.get(), options_.maxWindowSize()));
  }
}

bool ZSTDStreamCodec::doUncompressStream(
    ByteRange& input, MutableByteRange& output, StreamCodec::FlushOp) {
  if (dctx_ == nullptr) {
    resetDCtx();
  }
  ZSTD_inBuffer in = {input.data(), input.size(), 0};
  ZSTD_outBuffer out = {output.data(), output.size(), 0};
  FOLLY_SCOPE_EXIT {
    input.uncheckedAdvance(in.pos);
    output.uncheckedAdvance(out.pos);
  };
  size_t const rc =
      zstdThrowIfError(ZSTD_decompressStream(dctx_.get(), &out, &in));
  if (rc == 0) {
    // Surrender our dctx_
    doResetStream();
  }
  return rc == 0;
}

} // namespace

Options::Options(int level) : params_(ZSTD_createCCtxParams()), level_(level) {
  if (params_ == nullptr) {
    throw std::bad_alloc{};
  }
  zstdThrowIfError(ZSTD_CCtxParams_init(params_.get(), level));
}

void Options::set(ZSTD_cParameter param, unsigned value) {
  zstdThrowIfError(ZSTD_CCtxParams_setParameter(params_.get(), param, value));
  if (param == ZSTD_c_compressionLevel) {
    level_ = static_cast<int>(value);
  }
}

/* static */ void Options::freeCCtxParams(ZSTD_CCtx_params* params) {
  ZSTD_freeCCtxParams(params);
}

std::unique_ptr<Codec> getCodec(Options options) {
  return std::make_unique<ZSTDStreamCodec>(std::move(options));
}

std::unique_ptr<StreamCodec> getStreamCodec(Options options) {
  return std::make_unique<ZSTDStreamCodec>(std::move(options));
}

} // namespace zstd
} // namespace io
} // namespace folly

#endif
