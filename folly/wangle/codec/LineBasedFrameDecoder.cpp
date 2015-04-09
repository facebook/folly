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
#include <folly/wangle/codec/LineBasedFrameDecoder.h>

namespace folly { namespace wangle {

using folly::io::Cursor;

LineBasedFrameDecoder::LineBasedFrameDecoder(uint32_t maxLength,
                                             bool stripDelimiter,
                                             TerminatorType terminatorType)
    : maxLength_(maxLength)
    , stripDelimiter_(stripDelimiter)
    , terminatorType_(terminatorType) {}

std::unique_ptr<IOBuf> LineBasedFrameDecoder::decode(
  Context* ctx, IOBufQueue& buf, size_t&) {
  int64_t eol = findEndOfLine(buf);

  if (!discarding_) {
    if (eol >= 0) {
      Cursor c(buf.front());
      c += eol;
      auto delimLength = c.read<char>() == '\r' ? 2 : 1;
      if (eol > maxLength_) {
        buf.split(eol + delimLength);
        fail(ctx, folly::to<std::string>(eol));
        return nullptr;
      }

      std::unique_ptr<folly::IOBuf> frame;

      if (stripDelimiter_) {
        frame = buf.split(eol);
        buf.trimStart(delimLength);
      } else {
        frame = buf.split(eol + delimLength);
      }

      return std::move(frame);
    } else {
      auto len = buf.chainLength();
      if (len > maxLength_) {
        discardedBytes_ = len;
        buf.trimStart(len);
        discarding_ = true;
        fail(ctx, "over " + folly::to<std::string>(len));
      }
      return nullptr;
    }
  } else {
    if (eol >= 0) {
      Cursor c(buf.front());
      c += eol;
      auto delimLength = c.read<char>() == '\r' ? 2 : 1;
      buf.trimStart(eol + delimLength);
      discardedBytes_ = 0;
      discarding_ = false;
    } else {
      discardedBytes_ = buf.chainLength();
      buf.move();
    }

    return nullptr;
  }
}

void LineBasedFrameDecoder::fail(Context* ctx, std::string len) {
  ctx->fireReadException(
    folly::make_exception_wrapper<std::runtime_error>(
      "frame length" + len +
      " exeeds max " + folly::to<std::string>(maxLength_)));
}

int64_t LineBasedFrameDecoder::findEndOfLine(IOBufQueue& buf) {
  Cursor c(buf.front());
  for (uint32_t i = 0; i < maxLength_ && i < buf.chainLength(); i++) {
    auto b = c.read<char>();
    if (b == '\n' && terminatorType_ != TerminatorType::CARRIAGENEWLINE) {
      return i;
    } else if (terminatorType_ != TerminatorType::NEWLINE &&
               b == '\r' && !c.isAtEnd() && c.read<char>() == '\n') {
      return i;
    }
  }

  return -1;
}

}} // namespace
