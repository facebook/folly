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

#include <folly/CppAttributes.h>

#if FOLLY_HAVE_ELF
#include <link.h>
#endif

namespace folly {
namespace symbolizer {
namespace detail {

#if defined(__linux__) && FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
inline struct r_debug* get_r_debug() {
  return &_r_debug;
}
#elif defined(__APPLE__)
extern struct r_debug _r_debug;
inline struct r_debug* get_r_debug() {
  return &_r_debug;
}
#else
inline struct r_debug* get_r_debug() {
  return nullptr;
}
#endif

} // namespace detail
} // namespace symbolizer
} // namespace folly
