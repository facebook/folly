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

#include <folly/compression/CompressionContextPoolSingletons.h>

#include <stdlib.h>

#include <folly/Portability.h>
#include <folly/memory/Malloc.h>

#ifndef FOLLY_COMPRESSION_USE_HUGEPAGES
#if defined(__linux__) && !defined(__ANDROID__)
#define FOLLY_COMPRESSION_USE_HUGEPAGES 1
#else
#define FOLLY_COMPRESSION_USE_HUGEPAGES 0
#endif
#endif

#if FOLLY_COMPRESSION_USE_HUGEPAGES
#include <folly/memory/JemallocHugePageAllocator.h>
#endif

#if FOLLY_HAVE_LIBZSTD
#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h>
#endif

namespace folly {
namespace compression {
namespace contexts {

#if FOLLY_HAVE_LIBZSTD
namespace {
// These objects have no static dependencies and therefore no SIOF issues.
ZSTD_CCtx_Pool zstd_cctx_pool_singleton;
ZSTD_DCtx_Pool zstd_dctx_pool_singleton;

#if FOLLY_COMPRESSION_USE_HUGEPAGES
constexpr bool use_huge_pages = kIsArchAmd64;

void* huge_page_alloc(void*, size_t size) {
  if (size < 16 * 4096) {
    // Arbritrary cutoff: ZSTD_CCtx'es only ever make two kinds of allocations:
    // 1. one small one for the CCtx itself.
    // 2. "big" ones for the workspace (ZSTD_cwksp)
    // The CCtx allocation doesn't need to be in a huge page.
    return malloc(size);
  }
  return JemallocHugePageAllocator::allocate(size);
}

void huge_page_free(void*, void* address) {
  if (address != nullptr) {
    if (JemallocHugePageAllocator::addressInArena(address)) {
      JemallocHugePageAllocator::deallocate(address);
    } else {
      free(address);
    }
  }
}

ZSTD_customMem huge_page_custom_mem = (use_huge_pages && usingJEMalloc())
    ? (ZSTD_customMem){huge_page_alloc, huge_page_free, nullptr}
    : ZSTD_defaultCMem;
#else
ZSTD_customMem huge_page_custom_mem = ZSTD_defaultCMem;
#endif

} // anonymous namespace

ZSTD_CCtx* ZSTD_CCtx_Creator::operator()() const noexcept {
  return ZSTD_createCCtx_advanced(huge_page_custom_mem);
}

ZSTD_DCtx* ZSTD_DCtx_Creator::operator()() const noexcept {
  return ZSTD_createDCtx_advanced(huge_page_custom_mem);
}

void ZSTD_CCtx_Deleter::operator()(ZSTD_CCtx* ctx) const noexcept {
  ZSTD_freeCCtx(ctx);
}

void ZSTD_DCtx_Deleter::operator()(ZSTD_DCtx* ctx) const noexcept {
  ZSTD_freeDCtx(ctx);
}

void ZSTD_CCtx_Resetter::operator()(ZSTD_CCtx* ctx) const noexcept {
  size_t const err = ZSTD_CCtx_reset(ctx, ZSTD_reset_session_and_parameters);
  assert(!ZSTD_isError(err)); // This function doesn't actually fail
  (void)err;
}

void ZSTD_DCtx_Resetter::operator()(ZSTD_DCtx* ctx) const noexcept {
  size_t const err = ZSTD_DCtx_reset(ctx, ZSTD_reset_session_and_parameters);
  assert(!ZSTD_isError(err)); // This function doesn't actually fail
  (void)err;
}

ZSTD_CCtx_Pool::Ref getZSTD_CCtx() {
  return zstd_cctx_pool_singleton.get();
}

ZSTD_DCtx_Pool::Ref getZSTD_DCtx() {
  return zstd_dctx_pool_singleton.get();
}

ZSTD_CCtx_Pool::Ref getNULL_ZSTD_CCtx() {
  return zstd_cctx_pool_singleton.getNull();
}

ZSTD_DCtx_Pool::Ref getNULL_ZSTD_DCtx() {
  return zstd_dctx_pool_singleton.getNull();
}

ZSTD_CCtx_Pool& zstd_cctx_pool() {
  return zstd_cctx_pool_singleton;
}

ZSTD_DCtx_Pool& zstd_dctx_pool() {
  return zstd_dctx_pool_singleton;
}

size_t get_zstd_cctx_created_count() {
  return zstd_cctx_pool_singleton.created_count();
}

size_t get_zstd_dctx_created_count() {
  return zstd_dctx_pool_singleton.created_count();
}

#endif // FOLLY_HAVE_LIBZSTD

} // namespace contexts
} // namespace compression
} // namespace folly
