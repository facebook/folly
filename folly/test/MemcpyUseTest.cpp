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

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

extern "C" {
void* __folly_memcpy(
    void* __restrict dst, const void* __restrict src, size_t size);
}

extern "C" FOLLY_KEEP void check_c_memcpy(void* d, void* s, size_t n) {
  memcpy(d, s, n);
}

extern "C" FOLLY_KEEP void check_std_memcpy(void* d, void* s, size_t n) {
  std::memcpy(d, s, n);
}

extern "C" FOLLY_KEEP void check_folly_memcpy(void* d, void* s, size_t n) {
  __folly_memcpy(d, s, n);
}

TEST(MemcpyUseTest, empty) {
  // this empty test existing forces the keeps above, the native code of which
  // may be inspected to prove that the internal build rules actually replace
  // what would otherwise be calls to memcpy with calls to __folly_memcpy
}
