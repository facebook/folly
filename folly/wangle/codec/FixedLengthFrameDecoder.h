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
#pragma once

#include <folly/wangle/codec/ByteToMessageCodec.h>

namespace folly {namespace wangle {

/**
 * A decoder that splits the received IOBufs by the fixed number
 * of bytes. For example, if you received the following four
 * fragmented packets:
 *
 * +---+----+------+----+
 * | A | BC | DEFG | HI |
 * +---+----+------+----+
 *
 * A FixedLengthFrameDecoder will decode them into the following three
 * packets with the fixed length:
 *
 * +-----+-----+-----+
 * | ABC | DEF | GHI |
 * +-----+-----+-----+
 *
 */
class FixedLengthFrameDecoder
  : public ByteToMessageCodec {
 public:

  FixedLengthFrameDecoder(size_t length)
    : length_(length) {}

  std::unique_ptr<IOBuf> decode(Context* ctx, IOBufQueue& q, size_t& needed) {
    if (q.chainLength() < length_) {
      needed = length_ - q.chainLength();
      return nullptr;
    }

    return q.split(length_);
  }

 private:
  size_t length_;
};

}} // Namespace
