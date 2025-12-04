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

#include <folly/compression/CompressionContextPool.h>
#include <folly/concurrency/CacheLocality.h>

namespace folly {
namespace compression {

/**
 * Non-templated base class which allows for generic interaction with context
 * pool instances.
 */
class CompressionCoreLocalContextPoolBase {
 public:
  virtual ~CompressionCoreLocalContextPoolBase() = default;

  virtual void setSize(size_t size) = 0;
};

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
template <
    typename T,
    typename Creator,
    typename Deleter,
    typename Resetter,
    typename Sizeof,
    typename Callback = CompressionContextPoolDefaultCallback>
class CompressionCoreLocalContextPool
    : public CompressionCoreLocalContextPoolBase {
 private:
  /**
   * Force each pointer to be on a different cache line.
   */
  class alignas(folly::hardware_destructive_interference_size) Storage {
   public:
    std::atomic<T*> ptr{nullptr};
  };

  class ReturnToPoolDeleter {
   public:
    using Pool = CompressionCoreLocalContextPool<
        T,
        Creator,
        Deleter,
        Resetter,
        Sizeof,
        Callback>;

    explicit ReturnToPoolDeleter(Pool* pool) : pool_(pool) { DCHECK(pool_); }

    void operator()(T* ptr) { pool_->store(ptr); }

   private:
    Pool* pool_;
  };

  using BackingPool =
      CompressionContextPool<T, Creator, Deleter, Resetter, Sizeof, Callback>;
  using BackingPoolRef = typename BackingPool::Ref;

 public:
  /**
   * The max size is derived from maximum stripes for folly::AccessSpreader.
   */
  static constexpr size_t kMaxNumStripes =
      folly::detail::AccessSpreaderBase::kMaxCpus;

  using Object = T;
  using Ref = std::unique_ptr<T, ReturnToPoolDeleter>;

  constexpr explicit CompressionCoreLocalContextPool(
      size_t numStripes = 8,
      Creator creator = Creator(),
      Deleter deleter = Deleter(),
      Resetter resetter = Resetter(),
      Sizeof size_of = Sizeof(),
      Callback callback = Callback())
      : numStripes_(numStripes),
        pool_(
            std::move(creator),
            std::move(deleter),
            std::move(resetter),
            std::move(size_of),
            std::move(callback)),
        caches_() {}

  ~CompressionCoreLocalContextPool() override { flush_shallow(); }

  Ref get() {
    auto ptr = local().ptr.exchange(nullptr);
    if (ptr == nullptr) {
      // no local ctx, get from backing pool
      ptr = pool_.get().release();
      DCHECK(ptr);
    }
    return Ref(ptr, get_deleter());
  }

  Ref getNull() { return Ref(nullptr, get_deleter()); }

  /**
   * Update the number of stripes. This will flush the pool if the stripe count
   * changes.
   */
  void setSize(size_t numStripes) override {
    if (numStripes == 0) {
      throw_exception<std::invalid_argument>(
          "CompressionCoreLocalContextPool must have at least 1 stripe");
    }
    if (numStripes > kMaxNumStripes) {
      DCHECK(false);
      numStripes = kMaxNumStripes;
    }

    auto before = numStripes_.exchange(numStripes);
    if (before != numStripes) {
      flush_shallow();
    }
  }

  size_t cacheSize() const { return numStripes_.load(); }

  size_t created_count() const { return pool_.created_count(); }

  void flush_deep() {
    flush_shallow();
    pool_.flush_deep();
  }

  void flush_shallow() {
    for (auto& cache : caches_) {
      // Return all cached contexts back to the backing pool.
      auto ptr = cache.ptr.exchange(nullptr);
      return_to_backing_pool(ptr);
    }
  }

 private:
  ReturnToPoolDeleter get_deleter() { return ReturnToPoolDeleter(this); }

  void store(T* ptr) {
    DCHECK(ptr);
    pool_.get_resetter()(ptr);
    auto other = local().ptr.exchange(ptr);
    if (other != nullptr) {
      return_to_backing_pool(other);
    }
  }

  void return_to_backing_pool(T* ptr) {
    BackingPoolRef(ptr, pool_.get_deleter());
  }

  Storage& local() {
    // Note that cachedCurrent(0) is valid, so this should be SIOF safe.
    const auto idx = folly::AccessSpreader<>::cachedCurrent(numStripes_);
    return caches_[idx];
  }

  relaxed_atomic<size_t> numStripes_;
  BackingPool pool_;

  /**
   * context_pool_max_num_stripes number of stripes are allocated to the
   * underlying stripes array. However, only the lower numStripes_ indices are
   * used to actually stripe the pool. This allows us to statically create
   * singletons while providing flexibility around how many stripes there are.
   */
  std::array<Storage, kMaxNumStripes> caches_;
};
} // namespace compression
} // namespace folly
