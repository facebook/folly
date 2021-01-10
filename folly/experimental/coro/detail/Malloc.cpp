/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/coro/detail/Malloc.h>

#include <folly/BenchmarkUtil.h>

#include <new>

extern "C" {

FOLLY_NOINLINE
void* folly_coro_async_malloc(std::size_t size) {
  void* p = ::operator new(size);

  // Add this after the call to prevent the compiler from
  // turning the call to operator new() into a tailcall.
  folly::doNotOptimizeAway(p);

  return p;
}

FOLLY_NOINLINE
void folly_coro_async_free(void* ptr, std::size_t size) {
#if __cpp_sized_deallocation >= 201309
  ::operator delete(ptr, size);
#else
  // sized delete is not available on iOS before 10.0
  (void)size;
  ::operator delete(ptr);
#endif

  // Add this after the call to prevent the compiler from
  // turning the call to operator delete() into a tailcall.
  folly::doNotOptimizeAway(size);
}
} // extern "C"
