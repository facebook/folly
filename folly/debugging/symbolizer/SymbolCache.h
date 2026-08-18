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

#include <cstdint>
#include <memory>

#include <folly/debugging/symbolizer/SymbolizedFrame.h>
#include <folly/small_vector.h>

namespace folly {
namespace symbolizer {

/**
 * Symbolized frames for a single address: the stacked inline calls, if any,
 * followed by the non-inlined call. Most addresses have no inlined frames, so
 * optimize for the single-entry case.
 */
using CachedSymbolizedFrames = folly::small_vector<SymbolizedFrame, 1>;

/**
 * A cache of symbolized frames, keyed by instruction address.
 *
 * Symbolizing an address costs tens of microseconds, a linear scan of an ELF
 * symbol table plus a DWARF lookup, so a hit is worth orders of magnitude more
 * than whatever the lookup costs. Implementations should optimize for hit
 * rate and for scaling across threads, not for the cost of a single operation.
 *
 * The cache is a pure optimization: dropping a lookup or an insertion is always
 * safe.
 *
 * Entries are keyed by address alone, so a cache may only be shared between
 * symbolizers using the same LocationInfoMode, since a DISABLED entry carries
 * no file and line information where a FAST one does, and using the same
 * ElfCacheBase, since a cached frame holds a shared_ptr to the ElfFile it was
 * resolved from and its name points into that file's mapped string table.
 *
 * Implementations are thread-safe.
 */
class SymbolCacheBase {
 public:
  virtual ~SymbolCacheBase() = default;

  /**
   * Copies the frames cached for `address` into `out` and returns true, or
   * leaves `out` untouched and returns false.
   *
   * Copying rather than lending is deliberate: the caller has to fill a
   * SymbolizedFrame array it owns, so it would copy regardless, and an
   * implementation is then free to hold whatever locks or hazard pointers it
   * likes for exactly the duration of the call.
   */
  virtual bool find(uintptr_t address, CachedSymbolizedFrames& out) = 0;

  /** Best effort: the entry may be dropped or evicted at any point. */
  virtual void insert(uintptr_t address, CachedSymbolizedFrames frames) = 0;
};

/**
 * An LRU cache of `capacity` entries, backed by a Synchronized
 * EvictingCacheMap.
 *
 * Every lookup takes the write lock, because EvictingCacheMap has to reorder
 * its eviction list on a hit, so readers serialize against each other.
 */
std::unique_ptr<SymbolCacheBase> makeLruSymbolCache(size_t capacity);

/**
 * A cache of roughly `capacity` entries, backed by a
 * folly::GenerationalCacheMap: lookups are lock-free, and an address that stops
 * appearing in stack traces is eventually evicted in favour of one that does.
 *
 * Prefer this to the LRU flavour wherever traces are symbolized often enough
 * for lookups to contend.
 */
std::unique_ptr<SymbolCacheBase> makeGenerationalSymbolCache(size_t capacity);

/** A cache that stores nothing. */
std::unique_ptr<SymbolCacheBase> makeNullSymbolCache();

/**
 * The process-wide cache for `mode`.
 *
 * There is one per LocationInfoMode because entries are keyed by address alone
 * and are not interchangeable across modes. All of them are tied to
 * defaultElfCache(): a cached frame holds a shared_ptr to the ElfFile it came
 * from and its name points into that file's mapped string table, so a
 * process-lifetime cache pins every object it has seen. Never hand one to a
 * Symbolizer built on a SignalSafeElfCache.
 *
 * Returns a null cache when caching is disabled by flag.
 */
SymbolCacheBase& getSharedSymbolCache(LocationInfoMode mode);

} // namespace symbolizer
} // namespace folly
