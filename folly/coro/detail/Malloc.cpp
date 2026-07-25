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

#include <folly/coro/detail/Malloc.h>

#include <folly/lang/Hint.h>
#include <folly/lang/New.h>

namespace {
// Clang may emit 256-bit aligned stores when initializing coroutine frames, so
// over-align frame allocations to match. Compilers that lack the coroutine
// alignment builtin don't perform such over-aligned stores and can use the
// default new alignment.
#if FOLLY_HAS_BUILTIN(__builtin_coro_align)
constexpr auto kCoroutineFrameAlignment = std::align_val_t{32};
#else
constexpr auto kCoroutineFrameAlignment =
    std::align_val_t{__STDCPP_DEFAULT_NEW_ALIGNMENT__};
#endif
} // namespace

extern "C" {

FOLLY_NOINLINE
void* folly_coro_async_malloc(std::size_t size) {
  auto p = folly::operator_new(size, kCoroutineFrameAlignment);

  // Add this after the call to prevent the compiler from
  // turning the call to operator new() into a tailcall.
  folly::compiler_must_not_elide(p);

  return p;
}

FOLLY_NOINLINE
void folly_coro_async_free(void* ptr, std::size_t size) {
  folly::operator_delete(ptr, size, kCoroutineFrameAlignment);

  // Add this after the call to prevent the compiler from
  // turning the call to operator delete() into a tailcall.
  folly::compiler_must_not_elide(size);
}
} // extern "C"
