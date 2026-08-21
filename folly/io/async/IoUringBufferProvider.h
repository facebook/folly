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

#include <variant>

#include <folly/io/async/IoUringDynamicProvidedBufferRing.h>
#include <folly/io/async/IoUringProvidedBufferRing.h>

#if FOLLY_HAS_LIBURING

namespace folly {

class IoUringBufferProvider {
 public:
  class LibUringCallError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  struct Options {
    uint16_t gid{0};
    uint32_t bufferCount{0};
    uint32_t bufferSize{0};
    bool useIncrementalBuffers{false};
  };

  struct Stats {
    uint32_t enobufCount{0};
    int utilPct{-1};
    uint16_t areaCount{0};

    auto operator<=>(const Stats&) const = default;
  };

  static std::unique_ptr<IoUringBufferProvider> create(
      io_uring* ioRingPtr, bool useDynamicRing, Options options);

  void enobuf() noexcept;
  uint32_t getAndResetEnobufCount() noexcept;

  std::unique_ptr<IOBuf> getIoBuf(
      uint16_t startBufId, size_t totalLength, bool hasMore) noexcept;
  std::unique_ptr<IOBuf> getIoBuf(const struct io_uring_cqe* cqe) noexcept;

  uint32_t count() const noexcept;
  bool available() const noexcept;
  size_t sizePerBuffer() const noexcept;
  uint16_t gid() const noexcept;
  int getUtilPct() const noexcept;
  uint16_t areaCount() const noexcept;

  void getStats(Stats& stats) noexcept {
    stats.enobufCount = getAndResetEnobufCount();
    stats.utilPct = getUtilPct();
    stats.areaCount = areaCount();
  }

 private:
  using Ring = std::variant<
      IoUringProvidedBufferRing::UniquePtr,
      IoUringDynamicProvidedBufferRing::UniquePtr>;

  explicit IoUringBufferProvider(Ring ring) : ring_(std::move(ring)) {}

  Ring ring_;
};

} // namespace folly

#endif
