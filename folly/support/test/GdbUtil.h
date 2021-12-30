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

#include <folly/CPortability.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace folly {

// Auto breakpoint in gdb.
FOLLY_ALWAYS_INLINE void asm_gdb_breakpoint() {
#ifdef _MSC_VER
  __debugbreak();
#elif FOLLY_ARM || FOLLY_AARCH64
  __asm__ volatile("svc 3");
#else
  // powerpc64, x86, ...
  __asm__ volatile("int $3");
#endif
}
} // namespace folly
