/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>

#include <folly/SpinLock.h>

namespace folly {
namespace detail {

/*
 * Stat digests, such as TDigest, can be expensive to merge. It is faster to
 * buffer writes and merge them in larger chunks. DigestBuilder buffers writes
 * to improve performance.
 *
 * The first bufferSize values are stored in a shared buffer. For cold stats,
 * this shared buffer minimizes memory usage at reasonable cpu cost. Warm stats
 * will fill the shared buffer, and begin to spill to cpu local buffers. Hot
 * stats will merge the cpu local buffers into cpu-local digests.
 */
template <typename DigestT>
class DigestBuilder {
 public:
  explicit DigestBuilder(size_t bufferSize, size_t digestSize);

  /*
   * Builds a DigestT from the buffer in a sync free manner. It is the
   * responsibility of the caller to synchronize with all appenders.
   */
  DigestT buildSyncFree() const;

  /*
   * Adds a value to the buffer.
   */
  void append(double value);

 private:
  struct alignas(hardware_destructive_interference_size) CpuLocalBuffer {
   public:
    mutable SpinLock mutex;
    std::vector<double> buffer;
    std::unique_ptr<DigestT> digest;
  };

  std::atomic<size_t> nextPos_;
  std::vector<CpuLocalBuffer> cpuLocalBuffers_;
  size_t digestSize_;
  std::atomic<bool> cpuLocalBuffersReady_;
  std::vector<double> buffer_;
};

} // namespace detail
} // namespace folly
