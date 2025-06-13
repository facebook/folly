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

#include <stddef.h>

// https://github.com/rust-lang/rust-bindgen/issues/2869
// For some reason, the pattern used by some standard libraries (e.g. mac)
//
// namespace std {
// using ::size_t;
// }
//
// Is not well understood by bindgen, leading to size_t mapping to u64 rather
// than usize. Applying this workaround here seems to work and keep the code
// less messy than the alternative, passing --no-size_t-is-usize to bindgen and
// using Into/TryInto to convert between u64 and usize.
namespace std {
using size_t = size_t;
}

#include <folly/io/IOBuf.h>

namespace facebook {
namespace rust {

std::unique_ptr<folly::IOBuf> iobuf_create(size_t cap) noexcept;
std::unique_ptr<folly::IOBuf> iobuf_create_combined(size_t cap) noexcept;
std::unique_ptr<folly::IOBuf> iobuf_create_chain(
    size_t total_cap, size_t buf_lim) noexcept;

folly::IOBuf* iobuf_take_ownership(
    void* buf,
    size_t capacity,
    size_t length,
    folly::IOBuf::FreeFunction freeFn = nullptr,
    void* userData = nullptr,
    bool freeOnError = true) noexcept;

void iobuf_prepend_chain(
    folly::IOBuf& iobuf, std::unique_ptr<folly::IOBuf> other) noexcept;
void iobuf_append_chain(
    folly::IOBuf& iobuf, std::unique_ptr<folly::IOBuf> other) noexcept;

bool iobuf_cmp_equal(const folly::IOBuf& a, const folly::IOBuf& b) noexcept;

} // namespace rust
} // namespace facebook
