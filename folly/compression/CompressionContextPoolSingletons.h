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

#pragma once

#include <folly/folly-config.h>

#if FOLLY_HAVE_LIBZSTD
#include <zstd.h>
#endif

#include <folly/compression/CompressionCoreLocalContextPool.h>

// When this header is present, folly/compression/Compression.h defines
// FOLLY_COMPRESSION_HAS_CONTEXT_POOL_SINGLETONS.

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

using ZSTD_CCtx_Pool = CompressionCoreLocalContextPool<
    ZSTD_CCtx,
    ZSTD_CCtx_Creator,
    ZSTD_CCtx_Deleter,
    4>;
using ZSTD_DCtx_Pool = CompressionCoreLocalContextPool<
    ZSTD_DCtx,
    ZSTD_DCtx_Creator,
    ZSTD_DCtx_Deleter,
    4>;

ZSTD_CCtx_Pool::Ref getZSTD_CCtx();

ZSTD_DCtx_Pool::Ref getZSTD_DCtx();

ZSTD_CCtx_Pool::Ref getNULL_ZSTD_CCtx();

ZSTD_DCtx_Pool::Ref getNULL_ZSTD_DCtx();

#endif // FOLLY_HAVE_LIBZSTD

} // namespace contexts
} // namespace compression
} // namespace folly
