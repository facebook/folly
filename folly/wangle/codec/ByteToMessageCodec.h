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

#include <folly/wangle/channel/Handler.h>

namespace folly { namespace wangle {

/**
 * A Handler which decodes bytes in a stream-like fashion from
 * IOBufQueue to a  Message type.
 *
 * Frame detection
 *
 * Generally frame detection should be handled earlier in the pipeline
 * by adding a DelimiterBasedFrameDecoder, FixedLengthFrameDecoder,
 * LengthFieldBasedFrameDecoder, LineBasedFrameDecoder.
 *
 * If a custom frame decoder is required, then one needs to be careful
 * when implementing one with {@link ByteToMessageDecoder}. Ensure
 * there are enough bytes in the buffer for a complete frame by
 * checking {@link ByteBuf#readableBytes()}. If there are not enough
 * bytes for a complete frame, return without modify the reader index
 * to allow more bytes to arrive.
 *
 * To check for complete frames without modify the reader index, use
 * IOBufQueue.front(), without split() or pop_front().
 */
class ByteToMessageCodec
    : public BytesToBytesHandler {
 public:

  virtual std::unique_ptr<IOBuf> decode(
    Context* ctx, IOBufQueue& buf, size_t&) = 0;

  void read(Context* ctx, IOBufQueue& q);

 private:
  IOBufQueue q_;
};

}}
