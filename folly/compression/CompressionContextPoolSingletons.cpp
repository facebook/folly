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

#include <folly/compression/CompressionContextPoolSingletons.h>

namespace folly {
namespace compression {
namespace contexts {

#if FOLLY_HAVE_LIBZSTD
namespace {
// These objects have no static dependencies and therefore no SIOF issues.
ZSTD_CCtx_Pool zstd_cctx_pool_singleton;
ZSTD_DCtx_Pool zstd_dctx_pool_singleton;
} // anonymous namespace

ZSTD_CCtx* ZSTD_CCtx_Creator::operator()() const noexcept {
  return ZSTD_createCCtx();
}

ZSTD_DCtx* ZSTD_DCtx_Creator::operator()() const noexcept {
  return ZSTD_createDCtx();
}

void ZSTD_CCtx_Deleter::operator()(ZSTD_CCtx* ctx) const noexcept {
  ZSTD_freeCCtx(ctx);
}

void ZSTD_DCtx_Deleter::operator()(ZSTD_DCtx* ctx) const noexcept {
  ZSTD_freeDCtx(ctx);
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
#endif // FOLLY_HAVE_LIBZSTD

} // namespace contexts
} // namespace compression
} // namespace folly
