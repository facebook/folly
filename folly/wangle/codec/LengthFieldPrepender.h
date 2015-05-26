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
 * An encoder that prepends the length of the message.  The length value is
 * prepended as a binary form.
 *
 * For example, LengthFieldPrepender(2)will encode the
 * following 12-bytes string:
 *
 * +----------------+
 * | "HELLO, WORLD" |
 * +----------------+
 *
 * into the following:
 *
 * +--------+----------------+
 * + 0x000C | "HELLO, WORLD" |
 * +--------+----------------+
 *
 * If you turned on the lengthIncludesLengthFieldLength flag in the
 * constructor, the encoded data would look like the following
 * (12 (original data) + 2 (prepended data) = 14 (0xE)):
 *
 * +--------+----------------+
 * + 0x000E | "HELLO, WORLD" |
 * +--------+----------------+
 *
 */
class LengthFieldPrepender
: public OutboundBytesToBytesHandler {
 public:
  LengthFieldPrepender(
    int lengthFieldLength = 4,
    int lengthAdjustment = 0,
    bool lengthIncludesLengthField = false,
    bool networkByteOrder = true);

  Future<void> write(Context* ctx, std::unique_ptr<IOBuf> buf);

 private:
  int lengthFieldLength_;
  int lengthAdjustment_;
  bool lengthIncludesLengthField_;
  bool networkByteOrder_;
};

}} // namespace
