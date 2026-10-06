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

#include <folly/memory/IoUringArena.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <type_traits>

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/detail/StaticSingletonManager.h>
#include <folly/lang/Align.h>
#include <folly/memory/FixedRegionAllocator.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/String.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

#include <glog/logging.h>

#if defined(FOLLY_USE_JEMALLOC) && !defined(FOLLY_SANITIZE)
#define FOLLY_IO_URING_JEMALLOC_ARENA_SUPPORTED 1
#else
#define FOLLY_IO_URING_JEMALLOC_ARENA_SUPPORTED 0
#endif // defined(FOLLY_USE_JEMALLOC) && !FOLLY_SANITIZE

#if !FOLLY_IO_URING_JEMALLOC_ARENA_SUPPORTED
#undef MALLOCX_ARENA
#undef MALLOCX_TCACHE_NONE
#define MALLOCX_ARENA(x) 0
#define MALLOCX_TCACHE_NONE 0

#if !defined(JEMALLOC_VERSION_MAJOR) || (JEMALLOC_VERSION_MAJOR < 5)
using extent_hooks_t = struct extent_hooks_s;
using extent_alloc_t =
    void*(extent_hooks_t*, void*, size_t, size_t, bool*, bool*, unsigned int);
struct extent_hooks_s {
  extent_alloc_t* alloc;
};
#endif // JEMALLOC_VERSION_MAJOR

#endif // !FOLLY_IO_URING_JEMALLOC_ARENA_SUPPORTED

namespace folly {
namespace {

void printError(int err, const char* msg) {
  int savedErrno = std::exchange(errno, err);
  PLOG(ERROR) << msg;
  errno = savedErrno;
}

class Arena;

Arena& getArena();

class Arena {
 public:
  int init(size_t size);

  void* reserve(size_t size, size_t alignment);

  bool addressInArena(void* address) {
    auto* const addr = static_cast<std::byte*>(address);
    return start_ != nullptr && !std::less<>{}(addr, start_) &&
        std::less<>{}(addr, end_);
  }

  void* base() { return start_; }

  size_t regionSize() { return end_ - start_; }

  size_t freeSpace() { return end_ - freePtr_; }

  unsigned arenaIndex() { return arenaIndex_; }

 private:
  static void* allocHook(
      extent_hooks_t* extent,
      void* new_addr,
      size_t size,
      size_t alignment,
      bool* zero,
      bool* commit,
      unsigned arena_ind);

  std::byte* start_{nullptr};
  std::byte* end_{nullptr};
  std::byte* freePtr_{nullptr};
  extent_alloc_t* originalAlloc_{nullptr};
  extent_hooks_t extentHooks_{};
  unsigned arenaIndex_{0};
};

Arena& getArena() {
  static_assert(std::is_trivially_destructible_v<Arena>);
  return folly::detail::createGlobal<Arena, void>();
}

class FixedRegionArena {
 public:
  bool init(size_t size) {
    DCHECK(allocator_ == nullptr);
    void* ptr = mmap(
        nullptr,
        size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
        -1,
        0);
    if (ptr == MAP_FAILED) {
      printError(errno, "mmap failed for IoUringArena");
      return false;
    }
    try {
      allocator_ = new FixedRegionAllocator(ptr, size);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Unable to initialize IoUringArena allocator: "
                 << ex.what();
      munmap(ptr, size);
      return false;
    }
    return true;
  }

  void* allocate(size_t size) {
    if (void* ptr = allocator_->allocate(size)) {
      return ptr;
    }
    return malloc(size);
  }

  void* reallocate(void* ptr, size_t size) {
    if (!addressInArena(ptr)) {
      return realloc(ptr, size);
    }
    const auto oldSize = allocator_->allocationSize(ptr);
    if (void* replacement = allocator_->reallocate(ptr, size)) {
      return replacement;
    }
    void* replacement = malloc(size);
    if (replacement != nullptr) {
      memcpy(replacement, ptr, std::min(oldSize, size));
      allocator_->deallocate(ptr);
    }
    return replacement;
  }

  void deallocate(void* ptr) {
    if (addressInArena(ptr)) {
      allocator_->deallocate(ptr);
    } else {
      free(ptr);
    }
  }

  bool addressInArena(const void* address) const {
    return allocator_ != nullptr && allocator_->contains(address);
  }

  void* base() const { return allocator_ ? allocator_->data() : nullptr; }

  size_t regionSize() const { return allocator_ ? allocator_->size() : 0; }

  size_t freeSpace() const { return allocator_ ? allocator_->freeSpace() : 0; }

 private:
  // Process-lifetime ownership permits deallocation during static destruction.
  FixedRegionAllocator* allocator_{nullptr};
};

FixedRegionArena& getFixedRegionArena() {
  static_assert(std::is_trivially_destructible_v<FixedRegionArena>);
  return folly::detail::createGlobal<FixedRegionArena, void>();
}

struct IoUringArenaGlobalState {
  std::mutex initMutex;
  std::atomic<int> flags{0};
  std::atomic<bool> usingJemalloc{false};
};

IoUringArenaGlobalState& getIoUringArenaGlobalState() {
  return folly::detail::createGlobal<IoUringArenaGlobalState, void>();
}

bool jemallocArenaAvailable() {
  static const bool kUsingJemallocArena{
      FOLLY_IO_URING_JEMALLOC_ARENA_SUPPORTED && folly::usingJEMalloc()};
  return kUsingJemallocArena;
}

bool usingJemallocArena() {
  return getIoUringArenaGlobalState().usingJemalloc.load(
      std::memory_order_acquire);
}

int arenaFlags() {
  return getIoUringArenaGlobalState().flags.load(std::memory_order_acquire);
}

int Arena::init(size_t size) {
  const static size_t kPageSize = sysconf(_SC_PAGESIZE);
  DCHECK(start_ == nullptr);
  DCHECK(usingJEMalloc());

  size_t len = sizeof(arenaIndex_);
  if (auto ret = mallctl("arenas.create", &arenaIndex_, &len, nullptr, 0)) {
    printError(ret, "Unable to create jemalloc arena");
    return 0;
  }

  // Set grow retained limit to stop jemalloc from
  // forever increasing the requested size after failed allocations.
  size_t growRetainedLimit = kPageSize;
  auto rtlKey =
      folly::to<std::string>("arena.", arenaIndex_, ".retain_grow_limit");
  if (auto ret = mallctl(
          rtlKey.c_str(),
          nullptr,
          nullptr,
          &growRetainedLimit,
          sizeof(growRetainedLimit))) {
    printError(ret, "Unable to set growth limit");
    return 0;
  }

  auto hooksKey =
      folly::to<std::string>("arena.", arenaIndex_, ".extent_hooks");
  extent_hooks_t* hooks;
  len = sizeof(extent_hooks_t*);
  if (auto ret = mallctl(hooksKey.c_str(), &hooks, &len, nullptr, 0)) {
    printError(ret, "Unable to get extent hooks");
    return 0;
  }
  originalAlloc_ = hooks->alloc;

  extentHooks_ = *hooks;
  extentHooks_.alloc = &allocHook;
  extent_hooks_t* newHooks = &extentHooks_;
  if (auto ret = mallctl(
          hooksKey.c_str(),
          nullptr,
          nullptr,
          &newHooks,
          sizeof(extent_hooks_t*))) {
    printError(ret, "Unable to set extent hooks");
    return 0;
  }

  ssize_t decayMs = -1;
  auto dirtyDecayKey =
      folly::to<std::string>("arena.", arenaIndex_, ".dirty_decay_ms");
  if (auto ret = mallctl(
          dirtyDecayKey.c_str(), nullptr, nullptr, &decayMs, sizeof(decayMs))) {
    printError(ret, "Unable to set dirty decay");
    return 0;
  }

  auto muzzyDecayKey =
      folly::to<std::string>("arena.", arenaIndex_, ".muzzy_decay_ms");
  if (auto ret = mallctl(
          muzzyDecayKey.c_str(), nullptr, nullptr, &decayMs, sizeof(decayMs))) {
    printError(ret, "Unable to set muzzy decay");
    return 0;
  }

  void* ptr = mmap(
      nullptr,
      size,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
      -1,
      0);
  if (ptr == MAP_FAILED) {
    printError(errno, "mmap failed for IoUringArena");
    return 0;
  }

  start_ = freePtr_ = static_cast<std::byte*>(ptr);
  end_ = start_ + size;

  return MALLOCX_ARENA(arenaIndex_) | MALLOCX_TCACHE_NONE;
}

void* Arena::reserve(size_t size, size_t alignment) {
  if (!folly::valid_align_value(alignment)) {
    return nullptr;
  }
  auto* const result = folly::align_ceil(freePtr_, alignment);
  const auto padding = static_cast<size_t>(result - freePtr_);
  const auto remaining = static_cast<size_t>(end_ - freePtr_);
  if (padding > remaining || size > remaining - padding) {
    return nullptr;
  }
  freePtr_ = result + size;
  return result;
}

void* Arena::allocHook(
    extent_hooks_t* extent,
    void* new_addr,
    size_t size,
    size_t alignment,
    bool* zero,
    bool* commit,
    unsigned arena_ind) {
  void* res = nullptr;
  auto& arena = getArena();
  if (new_addr == nullptr) {
    res = arena.reserve(size, alignment);
  }
  if (res == nullptr) {
    res = arena.originalAlloc_(
        extent, new_addr, size, alignment, zero, commit, arena_ind);
  } else {
    if (*zero) {
      memset(res, 0, size);
    }
    *commit = true;
  }
  return res;
}

} // namespace

bool IoUringArena::init(size_t size) {
  auto& state = getIoUringArenaGlobalState();
  if (state.flags.load(std::memory_order_acquire) == 0) {
    std::lock_guard guard(state.initMutex);
    if (state.flags.load(std::memory_order_relaxed) == 0) {
      const bool useJemalloc = jemallocArenaAvailable();
      int flags = 0;
      if (useJemalloc) {
        flags = getArena().init(size);
      } else if (getFixedRegionArena().init(size)) {
        flags = 1;
      }
      if (flags != 0) {
        state.usingJemalloc.store(useJemalloc, std::memory_order_relaxed);
        state.flags.store(flags, std::memory_order_release);
      }
    }
  }
  return state.flags.load(std::memory_order_acquire) != 0;
}

void* IoUringArena::allocate(size_t size) {
  const auto flags = arenaFlags();
  if (flags == 0) {
    return malloc(size);
  }
  return usingJemallocArena()
      ? mallocx(size, flags)
      : getFixedRegionArena().allocate(size);
}

void* IoUringArena::reallocate(void* p, size_t size) {
  if (p == nullptr) {
    return allocate(size);
  }
  if (size == 0) {
    deallocate(p);
    return nullptr;
  }
  const auto flags = arenaFlags();
  if (flags == 0) {
    return realloc(p, size);
  }
  return usingJemallocArena()
      ? rallocx(p, size, flags)
      : getFixedRegionArena().reallocate(p, size);
}

void IoUringArena::deallocate(void* p, size_t) {
  const auto flags = arenaFlags();
  if (flags == 0) {
    free(p);
    p = nullptr;
  } else if (usingJemallocArena()) {
    dallocx(p, flags);
  } else {
    getFixedRegionArena().deallocate(p);
  }
}

void IoUringArena::deallocateOwnedBuffer(void* p, void*) noexcept {
  deallocate(p);
}

bool IoUringArena::initialized() {
  return arenaFlags() != 0;
}

bool IoUringArena::addressInArena(void* address) {
  if (arenaFlags() == 0) {
    return false;
  }
  return usingJemallocArena()
      ? getArena().addressInArena(address)
      : getFixedRegionArena().addressInArena(address);
}

void* IoUringArena::base() {
  if (arenaFlags() == 0) {
    return nullptr;
  }
  return usingJemallocArena()
      ? getArena().base()
      : getFixedRegionArena().base();
}

size_t IoUringArena::regionSize() {
  if (arenaFlags() == 0) {
    return 0;
  }
  return usingJemallocArena()
      ? getArena().regionSize()
      : getFixedRegionArena().regionSize();
}

size_t IoUringArena::freeSpace() {
  if (arenaFlags() == 0) {
    return 0;
  }
  return usingJemallocArena()
      ? getArena().freeSpace()
      : getFixedRegionArena().freeSpace();
}

unsigned IoUringArena::arenaIndex() {
  if (arenaFlags() == 0) {
    return 0;
  }
  return usingJemallocArena()
      ? getArena().arenaIndex()
      : kFixedRegionArenaIndex;
}

int IoUringArena::flags() {
  return arenaFlags();
}

} // namespace folly
