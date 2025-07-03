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

#include <folly/rust/iobuf/iobuf.h>

namespace facebook {
namespace rust {

using folly::IOBuf;

std::unique_ptr<IOBuf> iobuf_create(size_t cap) noexcept {
  return std::make_unique<IOBuf>(IOBuf::CREATE, cap);
}

std::unique_ptr<IOBuf> iobuf_create_combined(size_t cap) noexcept {
  return IOBuf::createCombined(cap);
}

std::unique_ptr<IOBuf> iobuf_create_chain(
    size_t total_cap, size_t buf_lim) noexcept {
  return IOBuf::createChain(total_cap, buf_lim);
}

IOBuf* iobuf_take_ownership(
    void* buf,
    size_t capacity,
    size_t length,
    IOBuf::FreeFunction freeFn,
    void* userData,
    bool freeOnError) noexcept {
  return new IOBuf(
      IOBuf::TAKE_OWNERSHIP,
      buf,
      capacity,
      length,
      freeFn,
      userData,
      freeOnError);
}

// Wrapper to pass a std::unique_ptr<>&& r-value reference, which CXX can't do
// yet. https://github.com/dtolnay/cxx/issues/561
void iobuf_prepend_chain(IOBuf& iobuf, std::unique_ptr<IOBuf> other) noexcept {
  iobuf.prependChain(std::move(other));
}

void iobuf_append_chain(IOBuf& iobuf, std::unique_ptr<IOBuf> other) noexcept {
  iobuf.appendChain(std::move(other));
}

bool iobuf_cmp_equal(const folly::IOBuf& a, const folly::IOBuf& b) noexcept {
  return folly::IOBufEqualTo{}(a, b);
}

} // namespace rust
} // namespace facebook
