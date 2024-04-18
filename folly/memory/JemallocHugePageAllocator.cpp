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

#include <folly/memory/JemallocHugePageAllocator.h>

#include <sstream>

#include <folly/CPortability.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/Malloc.h>
#include <folly/portability/String.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/SysTypes.h>

#include <glog/logging.h>

#if (defined(MADV_HUGEPAGE) || defined(MAP_ALIGNED_SUPER)) && \
    defined(FOLLY_USE_JEMALLOC) && !FOLLY_SANITIZE

#if defined(__FreeBSD__) || (JEMALLOC_VERSION_MAJOR >= 5)
#define FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED 1
#else
#define FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED 0
#endif // defined(__FreeBSD__) || (JEMALLOC_VERSION_MAJOR >= 5)

#else
#define FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED 0
#endif // MADV_HUGEPAGE || MAP_ALIGNED_SUPER && defined(FOLLY_USE_JEMALLOC) &&
       // !FOLLY_SANITIZE

#if !FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED
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
typedef void*(extent_alloc_t)(extent_hooks_t*,
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
  int init(int initial_nr_pages, int max_nr_pages);

  /* forces the system to actually back more pages */
  void init_more(int nr_pages);

  void* reserve(size_t size, size_t alignment);

  bool addressInArena(void* address) {
    auto addr = reinterpret_cast<uintptr_t>(address);
    return addr >= start_ && addr < protEnd_;
  }

  size_t freeSpace() { return end_ - freePtr_; }

  unsigned arenaIndex() { return arenaIndex_; }

 private:
  void map_pages(size_t initial_nr_pages, size_t max_nr_pages);

  bool setup_next_pages(uintptr_t nextFreePtr);

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
  uintptr_t protEnd_{0};
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
// If successful, sets the arena member pointers to reflect the mapped memory.
// Otherwise, leaves them unchanged (zeroed).
void HugePageArena::map_pages(size_t initial_nr_pages, size_t max_nr_pages) {
  // Initial mmapped area is large enough to contain the aligned huge pages
  size_t initial_alloc_size = initial_nr_pages * kHugePageSize;
  size_t max_alloc_size = max_nr_pages * kHugePageSize;
  int mflags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__FreeBSD__)
  mflags |= MAP_ALIGNED_SUPER;
#endif
  void* p =
      mmap(nullptr, max_alloc_size + kHugePageSize, PROT_NONE, mflags, -1, 0);

  if (p == MAP_FAILED) {
    return;
  }

  // Aligned start address
  uintptr_t first_page = align_up((uintptr_t)p, kHugePageSize);

#if !defined(__FreeBSD__)
  // Unmap left-over 4k pages
  const size_t excess_head = first_page - (uintptr_t)p;
  const size_t excess_tail = kHugePageSize - excess_head;
  if (excess_head != 0) {
    munmap(p, excess_head);
  }
  if (excess_tail != 0) {
    munmap((void*)(first_page + max_alloc_size), excess_tail);
  }
#endif

  start_ = freePtr_ = protEnd_ = first_page;
  end_ = start_ + max_alloc_size;

#if defined(MADV_DONTDUMP) && defined(MADV_DODUMP)
  // Exclude (possibly large) unused portion of the mapping from coredumps.
  madvise((void*)start_, end_ - start_, MADV_DONTDUMP);
#endif

  setup_next_pages(start_ + initial_alloc_size);
}

void HugePageArena::init_more(int nr_pages) {
  setup_next_pages(start_ + nr_pages * kHugePageSize);
}

// Warning: This can be called inside malloc(). Check the comments in
// HugePageArena::allocHook to understand the restrictions that imposes before
// making any change to this function.
// Requirement: upto > freePtr_ && upto > protEnd_.
// Returns whether the setup succeeded.
bool HugePageArena::setup_next_pages(uintptr_t upto) {
  const uintptr_t curPtr = protEnd_;
  const uintptr_t endPtr = align_up(upto, kHugePageSize);
  const size_t len = endPtr - curPtr;

  if (len == 0) {
    return true;
  }

  if (endPtr > end_) {
    return false;
  }

#if !defined(__FreeBSD__)
  // Tell the kernel to please give us huge pages for this range
  if (madvise((void*)curPtr, len, MADV_HUGEPAGE) != 0) {
    return false;
  }
#endif

  // Make this memory accessible.
  if (mprotect((void*)curPtr, len, PROT_READ | PROT_WRITE) != 0) {
    return false;
  }

#if defined(MADV_DONTDUMP) && defined(MADV_DODUMP)
  // Re-include these pages in coredumps now that we're using them.
  if (auto ret = madvise((void*)curPtr, len, MADV_DODUMP)) {
    print_error(ret, "Unable to madvise(MADV_DODUMP)");
  }
#endif

#if !defined(__FreeBSD__)
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
  for (uintptr_t ptr = curPtr; ptr < endPtr; ptr += kHugePageSize) {
    memset((void*)ptr, 0, 1);
  }
#endif

  protEnd_ = endPtr;
  return true;
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

int HugePageArena::init(int initial_nr_pages, int max_nr_pages) {
  DCHECK(start_ == 0);
  DCHECK(usingJEMalloc());

  if (max_nr_pages < initial_nr_pages) {
    max_nr_pages = initial_nr_pages;
  }

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

  map_pages(initial_nr_pages, max_nr_pages);
  if (start_ == 0) {
    return false;
  }
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
  if (newFreePtr > protEnd_) {
    if (!setup_next_pages(newFreePtr)) {
      return nullptr;
    }
  }
  freePtr_ = newFreePtr;
  return reinterpret_cast<void*>(res);
}

} // namespace

int JemallocHugePageAllocator::flags_{0};

bool JemallocHugePageAllocator::default_init() {
  // By default, map 1GB, but don't initialize anything. Individual users can
  // always ask for more pages to be readied.
  return init(0, 512);
}

bool JemallocHugePageAllocator::init(int initial_nr_pages, int max_nr_pages) {
  if (hugePagesAllocSupported()) {
    if (flags_ == 0) {
      flags_ = arena.init(initial_nr_pages, max_nr_pages);
    } else {
      /* was already initialized, let's just init the requested pages */
      arena.init_more(initial_nr_pages);
    }
  } else {
    LOG(WARNING) << "Huge Page Allocator not supported";
  }
  return flags_ != 0;
}

void* JemallocHugePageAllocator::allocate(size_t size) {
  // If uninitialized, flags_ will be 0 and the mallocx behavior
  // will match that of a regular malloc
  return hugePagesAllocSupported() ? mallocx(size, flags_) : malloc(size);
}

void* JemallocHugePageAllocator::reallocate(void* p, size_t size) {
  return hugePagesAllocSupported() ? rallocx(p, size, flags_)
                                   : realloc(p, size);
}

void JemallocHugePageAllocator::deallocate(void* p, size_t) {
  hugePagesAllocSupported() ? dallocx(p, flags_) : free(p);
}

bool JemallocHugePageAllocator::initialized() {
  return flags_ != 0;
}

size_t JemallocHugePageAllocator::freeSpace() {
  return arena.freeSpace();
}

bool JemallocHugePageAllocator::addressInArena(void* address) {
  return arena.addressInArena(address);
}

bool JemallocHugePageAllocator::hugePagesAllocSupported() {
  static const bool kHugePagesAllocSupported{
      FOLLY_JEMALLOC_HUGE_PAGE_ALLOCATOR_SUPPORTED && folly::usingJEMalloc()};
  return kHugePagesAllocSupported;
}

unsigned arenaIndex() {
  return arena.arenaIndex();
}

} // namespace folly
