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

#include <folly/Range.h>

// This is a workaround for a rust-bindgen/libclang bug. It looks like
// libclang isn't generating structural info for types unless it sees them
// actually being instantiated. Without this info, rust-bindgen just
// uses `u8` as a placeholder type. This dummy definition instantiates the
// types enough to make libclang generate the appropriate info.
// https://github.com/rust-lang-nursery/rust-bindgen/issues/1194
struct _dummy_instantiate_folly_string {
  folly::MutableStringPiece a;
  folly::MutableByteRange b;
  folly::StringPiece c;
  folly::ByteRange d;
};

namespace facebook {
namespace rust {

// StringPieces and ByteRanges don't own anything or have internal
// state, so don't need destructing
folly::StringPiece make_stringpiece(const char* base, size_t len);
const char* stringpiece_start(const folly::StringPiece* piece);
const char* stringpiece_end(const folly::StringPiece* piece);
size_t stringpiece_size(const folly::StringPiece* piece);

folly::MutableStringPiece make_mutablestringpiece(char* base, size_t len);
char* mutablestringpiece_start_mut(folly::MutableStringPiece* piece);
const char* mutablestringpiece_start(const folly::MutableStringPiece* piece);
char* mutablestringpiece_end(folly::MutableStringPiece* piece);
size_t mutablestringpiece_size(const folly::MutableStringPiece* piece);

folly::ByteRange make_byterange(const unsigned char* base, size_t len);
const unsigned char* byterange_start(const folly::ByteRange* piece);
const unsigned char* byterange_end(const folly::ByteRange* piece);
size_t byterange_size(const folly::ByteRange* piece);

folly::MutableByteRange make_mutablebyterange(unsigned char* base, size_t len);
unsigned char* mutablebyterange_start_mut(folly::MutableByteRange* piece);
const unsigned char* mutablebyterange_start(
    const folly::MutableByteRange* piece);
unsigned char* mutablebyterange_end(folly::MutableByteRange* piece);
size_t mutablebyterange_size(const folly::MutableByteRange* piece);

} // namespace rust
} // namespace facebook
