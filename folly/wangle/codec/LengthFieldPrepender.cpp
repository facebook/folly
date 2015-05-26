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
#include <folly/wangle/codec/LengthFieldPrepender.h>

namespace folly { namespace wangle {

LengthFieldPrepender::LengthFieldPrepender(
    int lengthFieldLength,
    int lengthAdjustment,
    bool lengthIncludesLengthField,
    bool networkByteOrder)
    : lengthFieldLength_(lengthFieldLength)
    , lengthAdjustment_(lengthAdjustment)
    , lengthIncludesLengthField_(lengthIncludesLengthField)
    , networkByteOrder_(networkByteOrder) {
    CHECK(lengthFieldLength == 1 ||
          lengthFieldLength == 2 ||
          lengthFieldLength == 4 ||
          lengthFieldLength == 8 );
  }

Future<void> LengthFieldPrepender::write(
    Context* ctx, std::unique_ptr<IOBuf> buf) {
  int length = lengthAdjustment_ + buf->computeChainDataLength();
  if (lengthIncludesLengthField_) {
    length += lengthFieldLength_;
  }

  if (length < 0) {
    throw std::runtime_error("Length field < 0");
  }

  auto len = IOBuf::create(lengthFieldLength_);
  len->append(lengthFieldLength_);
  folly::io::RWPrivateCursor c(len.get());

  switch (lengthFieldLength_) {
    case 1: {
      if (length >= 256) {
        throw std::runtime_error("length does not fit byte");
      }
      if (networkByteOrder_) {
        c.writeBE((uint8_t)length);
      } else {
        c.writeLE((uint8_t)length);
      }
      break;
    }
    case 2: {
      if (length >= 65536) {
        throw std::runtime_error("length does not fit byte");
      }
      if (networkByteOrder_) {
        c.writeBE((uint16_t)length);
      } else {
        c.writeLE((uint16_t)length);
      }
      break;
    }
    case 4: {
      if (networkByteOrder_) {
        c.writeBE((uint32_t)length);
      } else {
        c.writeLE((uint32_t)length);
      }
      break;
    }
    case 8: {
      if (networkByteOrder_) {
        c.writeBE((uint64_t)length);
      } else {
        c.writeLE((uint64_t)length);
      }
      break;
    }
    default: {
      throw std::runtime_error("Invalid lengthFieldLength");
    }
  }

  len->prependChain(std::move(buf));
  return ctx->fireWrite(std::move(len));
}


}} // Namespace
