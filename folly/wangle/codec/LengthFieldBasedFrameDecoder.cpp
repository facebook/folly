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
#include <folly/wangle/codec/LengthFieldBasedFrameDecoder.h>

namespace folly { namespace wangle {

LengthFieldBasedFrameDecoder::LengthFieldBasedFrameDecoder(
  uint32_t lengthFieldLength,
  uint32_t maxFrameLength,
  uint32_t lengthFieldOffset,
  uint32_t lengthAdjustment,
  uint32_t initialBytesToStrip,
  bool networkByteOrder)
    : lengthFieldLength_(lengthFieldLength)
    , maxFrameLength_(maxFrameLength)
    , lengthFieldOffset_(lengthFieldOffset)
    , lengthAdjustment_(lengthAdjustment)
    , initialBytesToStrip_(initialBytesToStrip)
    , networkByteOrder_(networkByteOrder)
    , lengthFieldEndOffset_(lengthFieldOffset + lengthFieldLength) {
  CHECK(maxFrameLength > 0);
  CHECK(lengthFieldOffset <= maxFrameLength - lengthFieldLength);
}

std::unique_ptr<IOBuf> LengthFieldBasedFrameDecoder::decode(
  Context* ctx, IOBufQueue& buf, size_t&) {
  // discarding too long frame
  if (buf.chainLength() < lengthFieldEndOffset_) {
    return nullptr;
  }

  uint64_t frameLength = getUnadjustedFrameLength(
    buf, lengthFieldOffset_, lengthFieldLength_, networkByteOrder_);

  frameLength += lengthAdjustment_ + lengthFieldEndOffset_;

  if (frameLength < lengthFieldEndOffset_) {
    buf.trimStart(lengthFieldEndOffset_);
    ctx->fireReadException(folly::make_exception_wrapper<std::runtime_error>(
                             "Frame too small"));
    return nullptr;
  }

  if (frameLength > maxFrameLength_) {
    buf.trimStart(frameLength);
    ctx->fireReadException(folly::make_exception_wrapper<std::runtime_error>(
                             "Frame larger than " +
                             folly::to<std::string>(maxFrameLength_)));
    return nullptr;
  }

  if (buf.chainLength() < frameLength) {
    return nullptr;
  }

  if (initialBytesToStrip_ > frameLength) {
    buf.trimStart(frameLength);
    ctx->fireReadException(folly::make_exception_wrapper<std::runtime_error>(
                             "InitialBytesToSkip larger than frame"));
    return nullptr;
  }

  buf.trimStart(initialBytesToStrip_);
  int actualFrameLength = frameLength - initialBytesToStrip_;
  return buf.split(actualFrameLength);
}

uint64_t LengthFieldBasedFrameDecoder::getUnadjustedFrameLength(
  IOBufQueue& buf, int offset, int length, bool networkByteOrder) {
  folly::io::Cursor c(buf.front());
  uint64_t frameLength;

  c.skip(offset);

  switch(length) {
    case 1:{
      if (networkByteOrder) {
        frameLength = c.readBE<uint8_t>();
      } else {
        frameLength = c.readLE<uint8_t>();
      }
      break;
    }
    case 2:{
      if (networkByteOrder) {
        frameLength = c.readBE<uint16_t>();
      } else {
        frameLength = c.readLE<uint16_t>();
      }
      break;
    }
    case 4:{
      if (networkByteOrder) {
        frameLength = c.readBE<uint32_t>();
      } else {
        frameLength = c.readLE<uint32_t>();
      }
      break;
    }
    case 8:{
      if (networkByteOrder) {
        frameLength = c.readBE<uint64_t>();
      } else {
        frameLength = c.readLE<uint64_t>();
      }
      break;
    }
  }

  return frameLength;
}


}} // namespace
