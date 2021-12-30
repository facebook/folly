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

#include <folly/SharedMutex.h>

#include <folly/Indestructible.h>

namespace folly {
// Explicitly instantiate SharedMutex here:
template class SharedMutexImpl<true>;
template class SharedMutexImpl<false>;

namespace shared_mutex_detail {
std::unique_lock<std::mutex> annotationGuard(void* ptr) {
  if (folly::kIsSanitizeThread) {
    // On TSAN builds, we have an array of mutexes and index into them based on
    // the address. If the array is of prime size things will work out okay
    // without a complicated hash function.
    static constexpr std::size_t kNumAnnotationMutexes = 251;
    static Indestructible<std::array<std::mutex, kNumAnnotationMutexes>>
        kAnnotationMutexes;
    auto index = reinterpret_cast<uintptr_t>(ptr) % kNumAnnotationMutexes;
    return std::unique_lock<std::mutex>((*kAnnotationMutexes)[index]);
  } else {
    return std::unique_lock<std::mutex>();
  }
}

uint32_t getMaxDeferredReadersSlow(std::atomic<uint32_t>& cache) {
  uint32_t maxDeferredReaders = std::min(
      static_cast<uint32_t>(
          folly::nextPowTwo(CacheLocality::system().numCpus) << 1),
      shared_mutex_detail::kMaxDeferredReadersAllocated);
  // maxDeferredReaders must be a power of 2
  assert(!(maxDeferredReaders & (maxDeferredReaders - 1)));
  cache.store(maxDeferredReaders, std::memory_order_release);
  return maxDeferredReaders;
}
} // namespace shared_mutex_detail
} // namespace folly
