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
#include <folly/lang/New.h>

#include <cstddef>

extern "C" {

// Heap allocations for coroutine-frames for all async coroutines
// (Task, AsyncGenerator, etc.) should be funneled through these
// functions to allow better tracing/profiling of coroutine allocations.
FOLLY_NOINLINE
void* folly_coro_async_malloc(std::size_t size);

FOLLY_NOINLINE
void folly_coro_async_free(void* ptr, std::size_t size);

FOLLY_NOINLINE
void* folly_coro_async_malloc_aligned(std::size_t size, std::size_t alignment);

FOLLY_NOINLINE
void folly_coro_async_free_aligned(
    void* ptr, std::size_t size, std::size_t alignment);
} // extern "C"

namespace folly::coro::detail {

struct CoroFrameAsyncAllocator {
  FOLLY_ALWAYS_INLINE static void* allocate(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }

  FOLLY_ALWAYS_INLINE static void* allocate(
      std::size_t size, std::size_t alignment) {
    return ::folly_coro_async_malloc_aligned(size, alignment);
  }

  FOLLY_ALWAYS_INLINE static void deallocate(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  FOLLY_ALWAYS_INLINE static void deallocate(
      void* ptr, std::size_t size, std::size_t alignment) {
    ::folly_coro_async_free_aligned(ptr, size, alignment);
  }
};

struct CoroFrameDefaultAllocator {
  FOLLY_ALWAYS_INLINE static void* allocate(std::size_t size) {
    return folly::operator_new(size);
  }

  FOLLY_ALWAYS_INLINE static void* allocate(
      std::size_t size, std::size_t alignment) {
    return folly::operator_new(size, std::align_val_t{alignment});
  }

  FOLLY_ALWAYS_INLINE static void deallocate(void* ptr, std::size_t size) {
    folly::operator_delete(ptr, size);
  }

  FOLLY_ALWAYS_INLINE static void deallocate(
      void* ptr, std::size_t size, std::size_t alignment) {
    folly::operator_delete(ptr, size, std::align_val_t{alignment});
  }
};

// These helpers and the class-specific operators which call them must remain
// always-inline so __builtin_coro_align is resolved in the allocating
// coroutine, including in unoptimized builds.
FOLLY_ALWAYS_INLINE std::size_t coro_frame_extended_alignment() {
#if FOLLY_HAS_BUILTIN(__builtin_coro_align)
  auto const alignment = __builtin_coro_align();
  return alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__ ? alignment : 0;
#else
  return 0;
#endif
}

template <typename Allocator>
FOLLY_ALWAYS_INLINE void* coro_frame_allocate(std::size_t size) {
  auto const alignment = coro_frame_extended_alignment();
  return alignment == 0
      ? Allocator::allocate(size)
      : Allocator::allocate(size, alignment);
}

template <typename Allocator>
FOLLY_ALWAYS_INLINE void coro_frame_deallocate(void* ptr, std::size_t size) {
  auto const alignment = coro_frame_extended_alignment();
  return alignment == 0
      ? Allocator::deallocate(ptr, size)
      : Allocator::deallocate(ptr, size, alignment);
}

} // namespace folly::coro::detail

#define FOLLY_CORO_DEFINE_FRAME_ALLOCATOR(Allocator)                    \
  FOLLY_ALWAYS_INLINE static void* operator new(std::size_t size) {     \
    return ::folly::coro::detail::coro_frame_allocate<Allocator>(size); \
  }                                                                     \
  FOLLY_ALWAYS_INLINE static void operator delete(                      \
      void* ptr, std::size_t size) {                                    \
    ::folly::coro::detail::coro_frame_deallocate<Allocator>(ptr, size); \
  }

#if FOLLY_HAS_BUILTIN(__builtin_coro_align)
#define FOLLY_CORO_DEFINE_FRAME_ALLOCATOR_IF_SUPPORTED(Allocator) \
  FOLLY_CORO_DEFINE_FRAME_ALLOCATOR(Allocator)
#else
#define FOLLY_CORO_DEFINE_FRAME_ALLOCATOR_IF_SUPPORTED(Allocator)
#endif
