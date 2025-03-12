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

#include <type_traits>

#include <folly/compression/Compression.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/lang/Bits.h>

/**
 * Helper functions for compression codecs.
 */
namespace folly {
namespace compression {
namespace detail {

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
  io::Cursor cursor{data};
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

/**
 * Calls @p streamFn in a loop, and guarantees that @p streamFn is never called
 * with `input.size() > chunkSize` or `output.size() > chunkSize`. It behaves
 * as-if streamFn(input, output, flushOp) was called.
 *
 * This function relies only on the rules defined by
 * StreamCodec::compressStream() and StreamCodec::uncompressStream() for
 * correctness.
 */
template <typename StreamFn>
bool chunkedStream(
    size_t chunkSize,
    ByteRange& input,
    MutableByteRange& output,
    StreamCodec::FlushOp flushOp,
    StreamFn&& streamFn) {
  for (;;) {
    const auto chunkInputSize = std::min<size_t>(input.size(), chunkSize);
    const auto chunkOutputSize = std::min<size_t>(output.size(), chunkSize);
    auto chunkInput = input.subpiece(0, chunkInputSize);
    auto chunkOutput = output.subpiece(0, chunkOutputSize);

    // If we're presenting the entire suffix of the input in this call, use the
    // users flush op, otherwise use NONE.
    const StreamCodec::FlushOp chunkFlushOp =
        (chunkInput.size() == input.size())
        ? flushOp
        : StreamCodec::FlushOp::NONE;

    const bool finished = streamFn(chunkInput, chunkOutput, chunkFlushOp);

    // Update input / output buffers
    const size_t inputConsumed = chunkInputSize - chunkInput.size();
    const size_t outputProduced = chunkOutputSize - chunkOutput.size();
    input.advance(inputConsumed);
    output.advance(outputProduced);

    // If the underlying streaming function returns true, we want to forward
    // that to the caller.
    // Compression: If flushOp == NONE, this is guaranteed not to happen.
    // Decompression: This signals the end of a frame, and we must forward that
    //                signal to the caller without consuming any more input.
    if (finished) {
      return true;
    }

    // We've consumed the entire input, which means we fulfilled our as-if
    // guarantee.
    if (input.empty()) {
      DCHECK(!finished);
      return false;
    }

    // Compression: Presenting more input bytes guarantees that there must be
    //              more output bytes to produce. Therefore we've made maximal
    //              forward progress.
    // Decompression: The only flushOp that guarantees maximal forward progress
    //                is FLUSH. Its signal that the flush is complete is
    //                !output.empty(). So we can safely return if
    //                output.empty().
    if (output.empty()) {
      DCHECK(!input.empty() && !finished);
      return false;
    }

    // If we've failed to make forward progress, return to the caller.
    // The underlying streaming function is always required to make some forward
    // progress if any forward progress is possible. So forward progress must be
    // impossible.
    // This preserves StreamCodec's strong guarantee of forward progress, and
    // protects us from infinite loops.
    if (inputConsumed == 0 && outputProduced == 0) {
      DCHECK(!output.empty() && !input.empty() && !finished);
      return false;
    }
  }
}

// Some codecs use uint32_t for sizes so we need to limit the chunk size to 4GB.
// Rather than allow 4GB inputs / outputs, which nearly never happens, limit to
// 4MB, so we don't have complex logic that almost never runs.
constexpr size_t kDefaultChunkSizeFor32BitSizes = size_t(4) << 20;

} // namespace detail
} // namespace compression
} // namespace folly
