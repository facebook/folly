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

#include <folly/lang/CString.h>

#include <algorithm>
#include <cstring>
#include <type_traits>

#include <folly/CppAttributes.h>
#include <folly/functional/Invoke.h>

namespace {

struct poison {};

[[maybe_unused]] FOLLY_ERASE void memrchr(poison) noexcept {}

} // namespace

namespace folly {

namespace detail {

void* memrchr_fallback(void* s, int c, std::size_t len) noexcept {
  return const_cast<void*>(
      memrchr_fallback(const_cast<void const*>(s), c, len));
}

void const* memrchr_fallback(void const* s, int c, std::size_t len) noexcept {
  auto const ss = static_cast<unsigned char const*>(s);
  for (auto it = ss + len - 1; it >= ss; --it) {
    if (*it == static_cast<unsigned char>(c)) {
      return it;
    }
  }
  return nullptr;
}

namespace {

FOLLY_CREATE_QUAL_INVOKER(invoke_primary_memrchr_fn, ::memrchr);
FOLLY_CREATE_QUAL_INVOKER(invoke_fallback_memrchr_fn, memrchr_fallback);

using invoke_memrchr_fn = invoke_first_match<
    invoke_primary_memrchr_fn,
    invoke_fallback_memrchr_fn,
    tag_t<>>;
constexpr invoke_memrchr_fn invoke_memrchr{};

} // namespace

} // namespace detail

void* memrchr(void* s, int c, std::size_t len) noexcept {
  return detail::invoke_memrchr(s, c, len);
}
void const* memrchr(void const* s, int c, std::size_t len) noexcept {
  return detail::invoke_memrchr(s, c, len);
}

std::size_t strlcpy(
    char* const dest, char const* const src, std::size_t const size) {
  std::size_t const len = std::strlen(src);
  if (size != 0) {
    std::size_t const n = std::min(len, size - 1); // always null terminate!
    std::memcpy(dest, src, n);
    dest[n] = '\0';
  }
  return len;
}

} // namespace folly
