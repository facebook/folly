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

#include <memory>
#include <vector>

#include <folly/Memory.h>
#include <folly/SpinLock.h>

namespace folly {

/*
 * Stat digests, such as TDigest, can be expensive to merge. It is faster to
 * buffer writes and merge them in larger chunks. DigestBuilder buffers writes
 * to improve performance.
 *
 * Values are stored in a cpu local buffer. Hot stats will merge the cpu local
 * buffer into a cpu-local digest when the buffer size is reached.
 *
 * All methods in this class are thread safe, but it probably doesn't make sense
 * for multiple threads to call build simultaneously. A typical usage is to
 * buffer writes for a period of time, and then have one thread call build to
 * merge the buffer into some other DigestT instance.
 */
template <typename DigestT>
class DigestBuilder {
 public:
  explicit DigestBuilder(size_t bufferSize, size_t digestSize);

  /*
   * Builds a DigestT from the buffer. All values used to build the DigestT are
   * removed from the buffer.
   */
  DigestT build();

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

    CpuLocalBuffer() noexcept = default;

    CpuLocalBuffer(CpuLocalBuffer&& other) noexcept
        : buffer{std::move(other.buffer)}, digest{std::move(other.digest)} {}

    CpuLocalBuffer& operator=(CpuLocalBuffer&& other) noexcept {
      if (this != &other) {
        buffer = std::move(other.buffer);
        digest = std::move(other.digest);
      }
      return *this;
    }
  };

  //  cpulocalbuffer_alloc custom allocator is necessary until C++17
  //    http://open-std.org/JTC1/SC22/WG21/docs/papers/2012/n3396.htm
  //    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=65122
  //    https://bugs.llvm.org/show_bug.cgi?id=22634
  using cpulocalbuffer_alloc =
      AlignedSysAllocator<CpuLocalBuffer, FixedAlign<alignof(CpuLocalBuffer)>>;
  std::vector<CpuLocalBuffer, cpulocalbuffer_alloc> cpuLocalBuffers_;
  size_t bufferSize_;
  size_t digestSize_;
};

} // namespace folly

#include <folly/stats/DigestBuilder-inl.h>
