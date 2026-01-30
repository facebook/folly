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

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/lang/Align.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/String.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

#include <glog/logging.h>

#if defined(FOLLY_USE_JEMALLOC) && !defined(FOLLY_SANITIZE)
#define FOLLY_IO_URING_ARENA_SUPPORTED 1
#else
#define FOLLY_IO_URING_ARENA_SUPPORTED 0
#endif // defined(FOLLY_USE_JEMALLOC) && !FOLLY_SANITIZE

#if !FOLLY_IO_URING_ARENA_SUPPORTED
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

#endif // !FOLLY_IO_URING_ARENA_SUPPORTED

namespace folly {
namespace {

void printError(int err, const char* msg) {
  int savedErrno = std::exchange(errno, err);
  PLOG(ERROR) << msg;
  errno = savedErrno;
}

class Arena {
 public:
  int init(size_t size);

  void* reserve(size_t size, size_t alignment);

  bool addressInArena(void* address) {
    auto addr = reinterpret_cast<uintptr_t>(address);
    return addr >= start_ && addr < end_;
  }

  void* base() { return reinterpret_cast<void*>(start_); }

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

  uintptr_t start_{0};
  uintptr_t end_{0};
  uintptr_t freePtr_{0};
  extent_alloc_t* originalAlloc_{nullptr};
  extent_hooks_t extentHooks_{};
  unsigned arenaIndex_{0};
};

Arena arena;

int Arena::init(size_t size) {
  const static size_t kPageSize = sysconf(_SC_PAGESIZE);
  DCHECK(start_ == 0);
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
  len = sizeof(hooks);
  if (auto ret = mallctl(hooksKey.c_str(), &hooks, &len, nullptr, 0)) {
    printError(ret, "Unable to get extent hooks");
    return 0;
  }
  originalAlloc_ = hooks->alloc;

  extentHooks_ = *hooks;
  extentHooks_.alloc = &allocHook;
  extent_hooks_t* newHooks = &extentHooks_;
  if (auto ret = mallctl(
          hooksKey.c_str(), nullptr, nullptr, &newHooks, sizeof(newHooks))) {
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

  start_ = freePtr_ = reinterpret_cast<uintptr_t>(ptr);
  end_ = start_ + size;

  return MALLOCX_ARENA(arenaIndex_) | MALLOCX_TCACHE_NONE;
}

void* Arena::reserve(size_t size, size_t alignment) {
  uintptr_t res = folly::align_ceil(freePtr_, alignment);
  uintptr_t newFreePtr = res + size;
  if (newFreePtr > end_) {
    return nullptr;
  }
  freePtr_ = newFreePtr;
  return reinterpret_cast<void*>(res);
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

int IoUringArena::flags_{0};

bool IoUringArena::init(size_t size) {
  if (ioUringArenaSupported()) {
    if (flags_ == 0) {
      flags_ = arena.init(size);
    }
  } else {
    LOG(WARNING) << "IoUringArena not supported";
  }
  return flags_ != 0;
}

void* IoUringArena::allocate(size_t size) {
  return ioUringArenaSupported() ? mallocx(size, flags_) : malloc(size);
}

void* IoUringArena::reallocate(void* p, size_t size) {
  return ioUringArenaSupported() ? rallocx(p, size, flags_) : realloc(p, size);
}

void IoUringArena::deallocate(void* p, size_t) {
  ioUringArenaSupported() ? dallocx(p, flags_) : free(p);
}

bool IoUringArena::initialized() {
  return flags_ != 0;
}

bool IoUringArena::addressInArena(void* address) {
  return arena.addressInArena(address);
}

void* IoUringArena::base() {
  return arena.base();
}

size_t IoUringArena::regionSize() {
  return arena.regionSize();
}

size_t IoUringArena::freeSpace() {
  return arena.freeSpace();
}

unsigned IoUringArena::arenaIndex() {
  return arena.arenaIndex();
}

int IoUringArena::flags() {
  return flags_;
}

bool IoUringArena::ioUringArenaSupported() {
  static const bool kIoUringArenaSupported{
      FOLLY_IO_URING_ARENA_SUPPORTED && folly::usingJEMalloc()};
  return kIoUringArenaSupported;
}

} // namespace folly
