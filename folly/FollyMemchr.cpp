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

#include <cstring>

namespace folly {
// Optimized memchr_long is only availble for Linux/ARM64, using std::memchr instead on other platforms
#if !(defined(__linux__) && defined(__aarch64__))
extern "C" void* memchr_long(const void* ptr, int ch, std::size_t count) {
  return std::memchr(ptr, ch, count);
}

} // namespace folly
#else
extern "C" {
void* __folly_memchr_long_aarch64(const void* dst, int c, std::size_t size);
void* __folly_memchr_long_aarch64_sha512(const void* dst, int c, std::size_t size);
}

extern "C" void* memchr_long(const void* ptr, int ch, std::size_t count) {
#ifdef FOLLY_ARM_FEATURE_SHA3
  return __folly_memchr_long_aarch64_sha512(ptr, ch, count);
#else
  return __folly_memchr_long_aarch64(ptr, ch, count);
#endif
}
#endif
} // namespace folly

