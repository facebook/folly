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

#include <folly/rust/compression/compression.h>

namespace facebook::folly_rust::compression {

using folly::StringPiece;
using folly::compression::CodecType;

bool has_codec(int32_t codec_type) {
  return folly::compression::hasCodec(static_cast<CodecType>(codec_type));
}

std::unique_ptr<std::string> compress_bytes(
    int32_t codec_type, rust::Slice<const uint8_t> data) {
  auto codec = folly::compression::getCodec(static_cast<CodecType>(codec_type));
  return std::make_unique<std::string>(codec->compress(
      StringPiece(reinterpret_cast<const char*>(data.data()), data.size())));
}

std::unique_ptr<std::string> uncompress_bytes(
    int32_t codec_type,
    rust::Slice<const uint8_t> data,
    uint64_t uncompressed_length) {
  auto codec = folly::compression::getCodec(static_cast<CodecType>(codec_type));
  return std::make_unique<std::string>(codec->uncompress(
      StringPiece(reinterpret_cast<const char*>(data.data()), data.size()),
      uncompressed_length));
}

} // namespace facebook::folly_rust::compression
