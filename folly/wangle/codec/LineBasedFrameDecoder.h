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
#include <folly/io/Cursor.h>

namespace folly { namespace wangle {

/**
 * A decoder that splits the received IOBufQueue on line endings.
 *
 * Both "\n" and "\r\n" are handled, or optionally reqire only
 * one or the other.
 */
class LineBasedFrameDecoder : public ByteToMessageCodec {
 public:
  enum class TerminatorType {
    BOTH,
    NEWLINE,
    CARRIAGENEWLINE
  };

  LineBasedFrameDecoder(uint32_t maxLength = UINT_MAX,
                        bool stripDelimiter = true,
                        TerminatorType terminatorType =
                        TerminatorType::BOTH);

  std::unique_ptr<IOBuf> decode(Context* ctx, IOBufQueue& buf, size_t&);

 private:

  int64_t findEndOfLine(IOBufQueue& buf);

  void fail(Context* ctx, std::string len);

  uint32_t maxLength_;
  bool stripDelimiter_;

  bool discarding_{false};
  uint32_t discardedBytes_{0};

  TerminatorType terminatorType_;
};

}} // namespace
