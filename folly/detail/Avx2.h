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

#include <folly/Portability.h>

#if defined(__AVX2__)

#include <immintrin.h>

#endif

namespace folly {
namespace detail {

#if defined(__AVX2__)

__m256i _mm256_loadu_si256_nosan(__m256i const* const p);
FOLLY_ERASE __m256i _mm256_loadu_si256_unchecked(__m256i const* const p) {
  return kIsSanitize ? _mm256_loadu_si256_nosan(p) : _mm256_loadu_si256(p);
}

__m256i _mm256_load_si256_nosan(__m256i const* const p);
FOLLY_ERASE __m256i _mm256_load_si256_unchecked(__m256i const* const p) {
  return kIsSanitize ? _mm256_load_si256_nosan(p) : _mm256_load_si256(p);
}

#endif

} // namespace detail
} // namespace folly
