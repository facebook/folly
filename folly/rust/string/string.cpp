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

#include <folly/rust/string/string.h>

namespace facebook {
namespace rust {

using namespace folly;

StringPiece make_stringpiece(const char* base, size_t len) {
  return StringPiece(base, len);
}

const char* stringpiece_start(const StringPiece* piece) {
  return piece->data();
}

const char* stringpiece_end(const StringPiece* piece) {
  return piece->end();
}

size_t stringpiece_size(const StringPiece* piece) {
  return piece->size();
}

MutableStringPiece make_mutablestringpiece(char* base, size_t len) {
  return MutableStringPiece(base, len);
}

char* mutablestringpiece_start_mut(MutableStringPiece* piece) {
  return piece->data();
}

const char* mutablestringpiece_start(const MutableStringPiece* piece) {
  return piece->data();
}

char* mutablestringpiece_end(MutableStringPiece* piece) {
  return piece->end();
}

size_t mutablestringpiece_size(const MutableStringPiece* piece) {
  return piece->size();
}

ByteRange make_byterange(const unsigned char* base, size_t len) {
  return ByteRange(base, len);
}

const unsigned char* byterange_start(const ByteRange* piece) {
  return piece->data();
}

const unsigned char* byterange_end(const ByteRange* piece) {
  return piece->end();
}

size_t byterange_size(const ByteRange* piece) {
  return piece->size();
}

MutableByteRange make_mutablebyterange(unsigned char* base, size_t len) {
  return MutableByteRange(base, len);
}

unsigned char* mutablebyterange_start_mut(MutableByteRange* piece) {
  return piece->data();
}

const unsigned char* mutablebyterange_start(const MutableByteRange* piece) {
  return piece->data();
}

unsigned char* mutablebyterange_end(MutableByteRange* piece) {
  return piece->end();
}

size_t mutablebyterange_size(const MutableByteRange* piece) {
  return piece->size();
}

} // namespace rust
} // namespace facebook
