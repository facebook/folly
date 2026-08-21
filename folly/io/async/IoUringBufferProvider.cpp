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

#include <folly/io/async/IoUringBufferProvider.h>

#if FOLLY_HAS_LIBURING

namespace folly {

namespace {

template <typename Ring>
typename Ring::UniquePtr createRing(
    io_uring* ioRingPtr, const IoUringBufferProvider::Options& options) {
  return Ring::create(
      ioRingPtr,
      {
          .gid = options.gid,
          .bufferCount = options.bufferCount,
          .bufferSize = options.bufferSize,
          .useIncrementalBuffers = options.useIncrementalBuffers,
      });
}

} // namespace

std::unique_ptr<IoUringBufferProvider> IoUringBufferProvider::create(
    io_uring* ioRingPtr, bool useDynamicRing, Options options) {
  try {
    if (useDynamicRing) {
      return std::unique_ptr<IoUringBufferProvider>(new IoUringBufferProvider(
          Ring{createRing<IoUringDynamicProvidedBufferRing>(
              ioRingPtr, options)}));
    }
    return std::unique_ptr<IoUringBufferProvider>(new IoUringBufferProvider(
        Ring{createRing<IoUringProvidedBufferRing>(ioRingPtr, options)}));
  } catch (const IoUringProvidedBufferRing::LibUringCallError& ex) {
    throw LibUringCallError(ex.what());
  } catch (const IoUringDynamicProvidedBufferRing::LibUringCallError& ex) {
    throw LibUringCallError(ex.what());
  }
}

void IoUringBufferProvider::enobuf() noexcept {
  std::visit([](auto& ring) { ring->enobuf(); }, ring_);
}

uint32_t IoUringBufferProvider::getAndResetEnobufCount() noexcept {
  return std::visit(
      [](auto& ring) { return ring->getAndResetEnobufCount(); }, ring_);
}

std::unique_ptr<IOBuf> IoUringBufferProvider::getIoBuf(
    uint16_t startBufId, size_t totalLength, bool hasMore) noexcept {
  return std::visit(
      [&](auto& ring) {
        return ring->getIoBuf(startBufId, totalLength, hasMore);
      },
      ring_);
}

std::unique_ptr<IOBuf> IoUringBufferProvider::getIoBuf(
    const struct io_uring_cqe* cqe) noexcept {
  return std::visit([&](auto& ring) { return ring->getIoBuf(cqe); }, ring_);
}

uint32_t IoUringBufferProvider::count() const noexcept {
  return std::visit([](const auto& ring) { return ring->count(); }, ring_);
}

bool IoUringBufferProvider::available() const noexcept {
  return std::visit([](const auto& ring) { return ring->available(); }, ring_);
}

size_t IoUringBufferProvider::sizePerBuffer() const noexcept {
  return std::visit(
      [](const auto& ring) { return ring->sizePerBuffer(); }, ring_);
}

uint16_t IoUringBufferProvider::gid() const noexcept {
  return std::visit([](const auto& ring) { return ring->gid(); }, ring_);
}

int IoUringBufferProvider::getUtilPct() const noexcept {
  return std::visit([](const auto& ring) { return ring->getUtilPct(); }, ring_);
}

uint16_t IoUringBufferProvider::areaCount() const noexcept {
  auto* ring = std::get_if<IoUringDynamicProvidedBufferRing::UniquePtr>(&ring_);
  if (!ring) {
    return 1;
  }
  return (*ring)->areaCount();
}

} // namespace folly

#endif
