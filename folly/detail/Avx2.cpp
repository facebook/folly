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

#include <folly/detail/Avx2.h>

#include <folly/CppAttributes.h>

namespace folly {
namespace detail {

#if defined(__AVX2__)

[[FOLLY_ATTR_GNU_FLATTEN]] FOLLY_DISABLE_SANITIZERS __m256i
_mm256_loadu_si256_nosan(__m256i const* const p) {
  return _mm256_loadu_si256(p);
}

[[FOLLY_ATTR_GNU_FLATTEN]] FOLLY_DISABLE_SANITIZERS __m256i
_mm256_load_si256_nosan(__m256i const* const p) {
  return _mm256_load_si256(p);
}

#endif

} // namespace detail
} // namespace folly
