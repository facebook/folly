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

#include <folly/experimental/JemallocHugePageAllocator.h>

#include <folly/portability/Malloc.h>
#include <folly/portability/String.h>
#include <glog/logging.h>

#include <sstream>

#if (defined(MADV_HUGEPAGE) || defined(MAP_ALIGNED_SUPER)) && \
    defined(FOLLY_USE_JEMALLOC) && !FOLLY_SANITIZE
#if defined(__FreeBSD__) || (JEMALLOC_VERSION_MAJOR >= 5)
#define FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED 1
bool folly::JemallocHugePageAllocator::hugePagesSupported{true};
#endif

#endif // MADV_HUGEPAGE || MAP_ALIGNED_SUPER && defined(FOLLY_USE_JEMALLOC) &&
       // !FOLLY_SANITIZE

#ifndef FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED
// Some mocks when jemalloc.h is not included or version too old
// or when the system does not support the MADV_HUGEPAGE madvise flag
#undef MALLOCX_ARENA
#undef MALLOCX_TCACHE_NONE
#undef MADV_HUGEPAGE
#define MALLOCX_ARENA(x) 0
#define MALLOCX_TCACHE_NONE 0
#define MADV_HUGEPAGE 0

#if !defined(JEMALLOC_VERSION_MAJOR) || (JEMALLOC_VERSION_MAJOR < 5)
typedef struct extent_hooks_s extent_hooks_t;
typedef void*(extent_alloc_t)(
    extent_hooks_t*,
    void*,
    size_t,
    size_t,
    bool*,
    bool*,
    unsigned);
struct extent_hooks_s {
  extent_alloc_t* alloc;
};
#endif // JEMALLOC_VERSION_MAJOR

bool folly::JemallocHugePageAllocator::hugePagesSupported{false};
#endif // FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED

namespace folly {
namespace {

void print_error(int err, const char* msg) {
  int cur_errno = std::exchange(errno, err);
  PLOG(ERROR) << msg;
  errno = cur_errno;
}

class HugePageArena {
 public:
  int init(int nr_pages);
  void* reserve(size_t size, size_t alignment);

  bool addressInArena(void* address) {
    auto addr = reinterpret_cast<uintptr_t>(address);
    return addr >= start_ && addr < end_;
  }

  size_t freeSpace() {
    return end_ - freePtr_;
  }

  unsigned arenaIndex() {
    return arenaIndex_;
  }

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
  extent_hooks_t extentHooks_;
  unsigned arenaIndex_{0};
};

constexpr size_t kHugePageSize = 2 * 1024 * 1024;

// Singleton arena instance
HugePageArena arena;

template <typename T, typename U>
static inline T align_up(T val, U alignment) {
  DCHECK((alignment & (alignment - 1)) == 0);
  return (val + alignment - 1) & ~(alignment - 1);
}

// mmap enough memory to hold the aligned huge pages, then use madvise
// to get huge pages. This can be checked in /proc/<pid>/smaps.
uintptr_t map_pages(size_t nr_pages) {
  // Initial mmapped area is large enough to contain the aligned huge pages
  size_t alloc_size = nr_pages * kHugePageSize;
  int mflags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__FreeBSD__)
  mflags |= MAP_ALIGNED_SUPER;
#endif
  void* p = mmap(
      nullptr,
      alloc_size + kHugePageSize,
      PROT_READ | PROT_WRITE,
      mflags,
      -1,
      0);

  if (p == MAP_FAILED) {
    return 0;
  }

  // Aligned start address
  uintptr_t first_page = align_up((uintptr_t)p, kHugePageSize);

#if !defined(__FreeBSD__)
  // Unmap left-over 4k pages
  munmap(p, first_page - (uintptr_t)p);
  munmap(
      (void*)(first_page + alloc_size),
      kHugePageSize - (first_page - (uintptr_t)p));

  // Tell the kernel to please give us huge pages for this range
  madvise((void*)first_page, kHugePageSize * nr_pages, MADV_HUGEPAGE);
  LOG(INFO) << nr_pages << " huge pages at " << (void*)first_page;

  // With THP set to madvise, page faults on these pages will block until a
  // huge page is found to service it. However, if memory becomes fragmented
  // before these pages are touched, then we end up blocking for kcompactd to
  // make a page available. This increases pressure to the point that oomd comes
  // in and kill us :(. So, preemptively touch these pages to get them backed
  // as early as possible to prevent stalling due to no available huge pages.
  //
  // Note: this does not guarantee we won't be oomd killed here, it's just much
  // more unlikely given this should be among the very first things an
  // application does.
  for (uintptr_t ptr = first_page; ptr < first_page + alloc_size;
       ptr += kHugePageSize) {
    memset((void*)ptr, 0, 1);
  }
#endif

  return first_page;
}

// WARNING WARNING WARNING
// This function is the hook invoked on malloc path for the hugepage allocator.
// This means it should not, itself, call malloc. If any of the following
// happens within this function, it *WILL* cause a DEADLOCK (from the circular
// dependency):
// - any dynamic memory allocation (i.e. calls to malloc)
// - any operations that may lead to dynamic operations, such as logging (e.g.
//   LOG, VLOG, LOG_IF) and DCHECK.
// WARNING WARNING WARNING
void* HugePageArena::allocHook(
    extent_hooks_t* extent,
    void* new_addr,
    size_t size,
    size_t alignment,
    bool* zero,
    bool* commit,
    unsigned arena_ind) {
  assert((size & (size - 1)) == 0);
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

int HugePageArena::init(int nr_pages) {
  DCHECK(start_ == 0);
  DCHECK(usingJEMalloc());

  size_t len = sizeof(arenaIndex_);
  if (auto ret = mallctl("arenas.create", &arenaIndex_, &len, nullptr, 0)) {
    print_error(ret, "Unable to create arena");
    return 0;
  }

  // Set grow retained limit to stop jemalloc from
  // forever increasing the requested size after failed allocations.
  // Normally jemalloc asks for maps of increasing size in order to avoid
  // hitting the limit of allowed mmaps per process.
  // Since this arena is backed by a single mmap and is using huge pages,
  // this is not a concern here.
  // TODO: Support growth of the huge page arena.
  size_t mib[3];
  size_t miblen = sizeof(mib) / sizeof(size_t);
  std::ostringstream rtl_key;
  rtl_key << "arena." << arenaIndex_ << ".retain_grow_limit";
  if (auto ret = mallctlnametomib(rtl_key.str().c_str(), mib, &miblen)) {
    print_error(ret, "Unable to read growth limit");
    return 0;
  }
  size_t grow_retained_limit = kHugePageSize;
  mib[1] = arenaIndex_;
  if (auto ret = mallctlbymib(
          mib,
          miblen,
          nullptr,
          nullptr,
          &grow_retained_limit,
          sizeof(grow_retained_limit))) {
    print_error(ret, "Unable to set growth limit");
    return 0;
  }

  std::ostringstream hooks_key;
  hooks_key << "arena." << arenaIndex_ << ".extent_hooks";
  extent_hooks_t* hooks;
  len = sizeof(hooks);
  // Read the existing hooks
  if (auto ret = mallctl(hooks_key.str().c_str(), &hooks, &len, nullptr, 0)) {
    print_error(ret, "Unable to get the hooks");
    return 0;
  }
  originalAlloc_ = hooks->alloc;

  // Set the custom hook
  extentHooks_ = *hooks;
  extentHooks_.alloc = &allocHook;
  extent_hooks_t* new_hooks = &extentHooks_;
  if (auto ret = mallctl(
          hooks_key.str().c_str(),
          nullptr,
          nullptr,
          &new_hooks,
          sizeof(new_hooks))) {
    print_error(ret, "Unable to set the hooks");
    return 0;
  }

  // Set dirty decay and muzzy decay time to -1, which will cause jemalloc
  // to never free memory to kernel.
  ssize_t decay_ms = -1;
  std::ostringstream dirty_decay_key;
  dirty_decay_key << "arena." << arenaIndex_ << ".dirty_decay_ms";
  if (auto ret = mallctl(
          dirty_decay_key.str().c_str(),
          nullptr,
          nullptr,
          &decay_ms,
          sizeof(decay_ms))) {
    print_error(ret, "Unable to set dirty decay time");
    return 0;
  }
  std::ostringstream muzzy_decay_key;
  muzzy_decay_key << "arena." << arenaIndex_ << ".muzzy_decay_ms";
  if (auto ret = mallctl(
          muzzy_decay_key.str().c_str(),
          nullptr,
          nullptr,
          &decay_ms,
          sizeof(decay_ms))) {
    print_error(ret, "Unable to set muzzy decay time");
    return 0;
  }

  start_ = freePtr_ = map_pages(nr_pages);
  if (start_ == 0) {
    return false;
  }
  end_ = start_ + (nr_pages * kHugePageSize);
  return MALLOCX_ARENA(arenaIndex_) | MALLOCX_TCACHE_NONE;
}

// Warning: Check the comments in HugePageArena::allocHook before making any
// change to this function.
void* HugePageArena::reserve(size_t size, size_t alignment) {
  uintptr_t res = align_up(freePtr_, alignment);
  uintptr_t newFreePtr = res + size;
  if (newFreePtr > end_) {
    return nullptr;
  }
  freePtr_ = newFreePtr;
  return reinterpret_cast<void*>(res);
}

} // namespace

int JemallocHugePageAllocator::flags_{0};

bool JemallocHugePageAllocator::init(int nr_pages) {
  if (!usingJEMalloc()) {
    LOG(ERROR) << "Not linked with jemalloc?";
    hugePagesSupported = false;
  }
  if (hugePagesSupported) {
    if (flags_ == 0) {
      flags_ = arena.init(nr_pages);
    } else {
      LOG(WARNING) << "Already initialized";
    }
  } else {
    LOG(WARNING) << "Huge Page Allocator not supported";
  }
  return flags_ != 0;
}

size_t JemallocHugePageAllocator::freeSpace() {
  return arena.freeSpace();
}

bool JemallocHugePageAllocator::addressInArena(void* address) {
  return arena.addressInArena(address);
}

unsigned arenaIndex() {
  return arena.arenaIndex();
}

} // namespace folly
