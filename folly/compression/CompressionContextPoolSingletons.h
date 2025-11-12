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

#include <folly/portability/Config.h>

#if FOLLY_HAVE_LIBZSTD
#include <zstd.h>
#endif

#include <folly/compression/CompressionCoreLocalContextPool.h>
#include <folly/portability/GFlags.h>

// When this header is present, folly/compression/Compression.h defines
// FOLLY_COMPRESSION_HAS_CONTEXT_POOL_SINGLETONS.

// These flags allow for tuning the number of stripes in the singleton
// context pool. This is expressed as a multiplier of CPU hardware
// concurrency, e.g. a setting of 0.1 on a machine with 50 cores results
// in 5 stripes in the context pool singleton.
FOLLY_GFLAGS_DECLARE_double(folly_zstd_cctx_pool_stripes_cpu_multiplier);
FOLLY_GFLAGS_DECLARE_double(folly_zstd_dctx_pool_stripes_cpu_multiplier);

namespace folly {
namespace compression {
namespace contexts {

#if FOLLY_HAVE_LIBZSTD

// Additional feature test macro for zstd singletons.
#define FOLLY_COMPRESSION_HAS_ZSTD_CONTEXT_POOL_SINGLETONS

struct ZSTD_CCtx_Creator {
  ZSTD_CCtx* operator()() const noexcept;
};

struct ZSTD_DCtx_Creator {
  ZSTD_DCtx* operator()() const noexcept;
};

struct ZSTD_CCtx_Deleter {
  void operator()(ZSTD_CCtx* ctx) const noexcept;
};

struct ZSTD_DCtx_Deleter {
  void operator()(ZSTD_DCtx* ctx) const noexcept;
};

struct ZSTD_CCtx_Resetter {
  void operator()(ZSTD_CCtx* ctx) const noexcept;
};

struct ZSTD_DCtx_Resetter {
  void operator()(ZSTD_DCtx* ctx) const noexcept;
};

struct ZSTD_CCtx_Sizeof {
  size_t operator()(const ZSTD_CCtx* ctx) const noexcept;
};

struct ZSTD_DCtx_Sizeof {
  size_t operator()(const ZSTD_DCtx* ctx) const noexcept;
};

class ZSTD_CCtx_Pool_Callback {
 public:
  explicit constexpr ZSTD_CCtx_Pool_Callback(
      CompressionCoreLocalContextPoolBase* pool)
      : pool_(pool) {}

  void operator()() const;

 private:
  CompressionCoreLocalContextPoolBase* pool_;
};

struct ZSTD_DCtx_Pool_Callback {
 public:
  explicit constexpr ZSTD_DCtx_Pool_Callback(
      CompressionCoreLocalContextPoolBase* pool)
      : pool_(pool) {}

  void operator()() const;

 private:
  CompressionCoreLocalContextPoolBase* pool_;
};

using ZSTD_CCtx_Pool = CompressionCoreLocalContextPool<
    ZSTD_CCtx,
    ZSTD_CCtx_Creator,
    ZSTD_CCtx_Deleter,
    ZSTD_CCtx_Resetter,
    ZSTD_CCtx_Sizeof,
    ZSTD_CCtx_Pool_Callback>;
using ZSTD_DCtx_Pool = CompressionCoreLocalContextPool<
    ZSTD_DCtx,
    ZSTD_DCtx_Creator,
    ZSTD_DCtx_Deleter,
    ZSTD_DCtx_Resetter,
    ZSTD_DCtx_Sizeof,
    ZSTD_DCtx_Pool_Callback>;

/**
 * Returns a clean ZSTD_CCtx.
 */
ZSTD_CCtx_Pool::Ref getZSTD_CCtx();

/**
 * Returns a clean ZSTD_DCtx.
 */
ZSTD_DCtx_Pool::Ref getZSTD_DCtx();

ZSTD_CCtx_Pool::Ref getNULL_ZSTD_CCtx();

ZSTD_DCtx_Pool::Ref getNULL_ZSTD_DCtx();

ZSTD_CCtx_Pool& zstd_cctx_pool();

ZSTD_DCtx_Pool& zstd_dctx_pool();

size_t get_zstd_cctx_created_count();

size_t get_zstd_dctx_created_count();

#endif // FOLLY_HAVE_LIBZSTD

} // namespace contexts
} // namespace compression
} // namespace folly
