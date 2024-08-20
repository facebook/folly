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

#include <cstddef>
#include <cstring>

#include <folly/CPortability.h>

#if FOLLY_HAS_BUILTIN(__builtin_memcpy_inline)
#define FOLLY_BUILTIN_MEMCPY(dest, src, size) \
  void(__builtin_memcpy_inline((dest), (src), (size)))
#elif FOLLY_HAS_BUILTIN(__builtin_memcpy)
#define FOLLY_BUILTIN_MEMCPY(dest, src, size) \
  void(__builtin_memcpy((dest), (src), (size)))
#else
#define FOLLY_BUILTIN_MEMCPY(dest, src, size) \
  void(::std::memcpy((dest), (src), (size)))
#endif

namespace folly {

namespace detail {

void* memrchr_fallback(void* s, int c, std::size_t len) noexcept;
void const* memrchr_fallback(void const* s, int c, std::size_t len) noexcept;

} // namespace detail

//  memrchr
//
//  mimic: memrchr, glibc++
void* memrchr(void* s, int c, std::size_t len) noexcept;
void const* memrchr(void const* s, int c, std::size_t len) noexcept;

//  strlcpy
//
//  mimic: strlcpy, libbsd
std::size_t strlcpy(char* dest, char const* src, std::size_t size);

} // namespace folly
