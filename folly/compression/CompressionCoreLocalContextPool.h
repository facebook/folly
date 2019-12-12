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

#include <folly/compression/CompressionContextPool.h>
#include <folly/concurrency/CacheLocality.h>

namespace folly {
namespace compression {

/**
 * This class is intended to reduce contention on reserving a compression
 * context and improve cache locality (but maybe not hotness) of the contexts
 * it manages.
 *
 * This class uses folly::AccessSpreader to spread the managed object across
 * NumStripes domains (which should correspond to a topologically close set of
 * hardware threads). This cache is still backed by the basic locked stack in
 * the folly::compression::CompressionContextPool.
 *
 * Note that there is a tradeoff in choosing the number of stripes. More stripes
 * make for less contention, but mean that a context is less likely to be hot
 * in cache.
 */
template <typename T, typename Creator, typename Deleter, size_t NumStripes = 8>
class CompressionCoreLocalContextPool {
 private:
  /**
   * Force each pointer to be on a different cache line.
   */
  class alignas(folly::hardware_destructive_interference_size) Storage {
   public:
    Storage() : ptr(nullptr) {}

    std::atomic<T*> ptr;
  };

  class ReturnToPoolDeleter {
   public:
    using Pool =
        CompressionCoreLocalContextPool<T, Creator, Deleter, NumStripes>;

    explicit ReturnToPoolDeleter(Pool* pool) : pool_(pool) {
      DCHECK(pool_);
    }

    void operator()(T* ptr) {
      pool_->store(ptr);
    }

   private:
    Pool* pool_;
  };

  using BackingPool = CompressionContextPool<T, Creator, Deleter>;
  using BackingPoolRef = typename BackingPool::Ref;

 public:
  using Object = T;
  using Ref = std::unique_ptr<T, ReturnToPoolDeleter>;

  explicit CompressionCoreLocalContextPool(
      Creator creator = Creator(),
      Deleter deleter = Deleter())
      : pool_(std::move(creator), std::move(deleter)), caches_() {}

  ~CompressionCoreLocalContextPool() {
    for (auto& cache : caches_) {
      // Return all cached contexts back to the backing pool.
      auto ptr = cache.ptr.exchange(nullptr);
      return_to_backing_pool(ptr);
    }
  }

  Ref get() {
    auto ptr = local().ptr.exchange(nullptr);
    if (ptr == nullptr) {
      // no local ctx, get from backing pool
      ptr = pool_.get().release();
      DCHECK(ptr);
    }
    return Ref(ptr, get_deleter());
  }

  Ref getNull() {
    return Ref(nullptr, get_deleter());
  }

 private:
  ReturnToPoolDeleter get_deleter() {
    return ReturnToPoolDeleter(this);
  }

  void store(T* ptr) {
    DCHECK(ptr);
    T* expected = nullptr;
    const bool stored = local().ptr.compare_exchange_weak(expected, ptr);
    if (!stored) {
      return_to_backing_pool(ptr);
    }
  }

  void return_to_backing_pool(T* ptr) {
    BackingPoolRef(ptr, pool_.get_deleter());
  }

  Storage& local() {
    const auto idx = folly::AccessSpreader<>::cachedCurrent(NumStripes);
    return caches_[idx];
  }

  BackingPool pool_;
  std::array<Storage, NumStripes> caches_{};
};
} // namespace compression
} // namespace folly
