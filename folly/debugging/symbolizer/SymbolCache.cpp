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

#include <folly/debugging/symbolizer/SymbolCache.h>

#include <array>
#include <utility>

#include <folly/Indestructible.h>
#include <folly/Synchronized.h>
#include <folly/container/EvictingCacheMap.h>
#include <folly/container/GenerationalCacheMap.h>
#include <folly/portability/Config.h>
#include <folly/portability/GFlags.h>

// Nothing here is reachable without a Symbolizer, which needs ELF and DWARF, so
// there is no reason to instantiate the machinery elsewhere.
#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

FOLLY_GFLAGS_DEFINE_uint64(
    folly_symbolizer_symbol_cache_capacity,
    50000,
    "Capacity of each process-wide symbol cache returned by "
    "folly::symbolizer::getSharedSymbolCache(), one per LocationInfoMode. An "
    "entry costs roughly 200 bytes, and a cache only grows to what it is "
    "actually used for. Zero disables symbol caching.");

namespace folly {
namespace symbolizer {

namespace {

class LruSymbolCache : public SymbolCacheBase {
 public:
  explicit LruSymbolCache(size_t capacity) : cache_(UnsyncCache{capacity}) {}

  bool find(uintptr_t address, CachedSymbolizedFrames& out) override {
    // A write lock even to read, because EvictingCacheMap moves a found entry
    // to the front of its eviction list.
    auto locked = cache_.wlock();
    const auto iter = locked->find(address);
    if (iter == locked->end()) {
      return false;
    }
    out = iter->second;
    return true;
  }

  void insert(uintptr_t address, CachedSymbolizedFrames frames) override {
    cache_.wlock()->set(address, std::move(frames));
  }

 private:
  using UnsyncCache = EvictingCacheMap<uintptr_t, CachedSymbolizedFrames>;
  using Cache = Synchronized<UnsyncCache>;

  Cache cache_;
};

class GenerationalSymbolCache : public SymbolCacheBase {
 public:
  explicit GenerationalSymbolCache(size_t capacity) : map_(capacity) {}

  bool find(uintptr_t address, CachedSymbolizedFrames& out) override {
    const auto found = map_.find(address);
    if (!found) {
      return false;
    }
    out = *found;
    return true;
  }

  void insert(uintptr_t address, CachedSymbolizedFrames frames) override {
    map_.set(address, std::move(frames));
  }

 private:
  GenerationalCacheMap<uintptr_t, CachedSymbolizedFrames> map_;
};

class NullSymbolCache : public SymbolCacheBase {
 public:
  bool find(uintptr_t, CachedSymbolizedFrames&) override { return false; }
  void insert(uintptr_t, CachedSymbolizedFrames) override {}
};

constexpr size_t kNumModes = static_cast<size_t>(LocationInfoMode::NUM_MODES);

} // namespace

std::unique_ptr<SymbolCacheBase> makeLruSymbolCache(size_t capacity) {
  return std::make_unique<LruSymbolCache>(capacity);
}

std::unique_ptr<SymbolCacheBase> makeGenerationalSymbolCache(size_t capacity) {
  return std::make_unique<GenerationalSymbolCache>(capacity);
}

std::unique_ptr<SymbolCacheBase> makeNullSymbolCache() {
  return std::make_unique<NullSymbolCache>();
}

SymbolCacheBase& getSharedSymbolCache(LocationInfoMode mode) {
  using Caches = std::array<std::unique_ptr<SymbolCacheBase>, kNumModes>;
  static Indestructible<Caches> caches([] {
    const auto capacity = FLAGS_folly_symbolizer_symbol_cache_capacity;
    Caches result;
    for (auto& cache : result) {
      cache = capacity > 0
          ? makeGenerationalSymbolCache(capacity)
          : makeNullSymbolCache();
    }
    return result;
  }());
  return *(*caches)[static_cast<size_t>(mode)];
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
