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

#include <folly/fibers/GuardPageAllocator.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <csignal>
#include <iostream>
#include <mutex>

#include <glog/logging.h>

#include <folly/Singleton.h>
#include <folly/SpinLock.h>
#include <folly/Synchronized.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

namespace folly {
namespace fibers {

/**
 * Each stack with a guard page creates two memory mappings.
 * Since this is a limited resource, we don't want to create too many of these.
 *
 * The upper bound on total number of mappings created
 * is kNumGuarded * kMaxInUse.
 */

/**
 * Number of guarded stacks per allocator instance
 */
constexpr size_t kNumGuarded = 100;

/**
 * Maximum number of allocator instances with guarded stacks enabled
 */
constexpr size_t kMaxInUse = 100;

/**
 * A cache for kNumGuarded stacks of a given size
 *
 * Thread safe.
 */
class StackCache {
 public:
  explicit StackCache(size_t stackSize, size_t guardPagesPerStack)
      : allocSize_(allocSize(stackSize, guardPagesPerStack)),
        guardPagesPerStack_(guardPagesPerStack) {
    auto p = ::mmap(
        nullptr,
        allocSize_ * kNumGuarded,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    PCHECK(p != (void*)(-1));
    storage_ = reinterpret_cast<unsigned char*>(p);

    /* Protect the bottommost page of every stack allocation */
    freeList_.reserve(kNumGuarded);
    for (size_t i = 0; i < kNumGuarded; ++i) {
      auto allocBegin = storage_ + allocSize_ * i;
      freeList_.emplace_back(allocBegin, /* protected= */ false);
    }
  }

  unsigned char* borrow(size_t size) {
    std::lock_guard<folly::SpinLock> lg(lock_);

    assert(storage_);

    auto as = allocSize(size, guardPagesPerStack_);
    if (as != allocSize_ || freeList_.empty()) {
      return nullptr;
    }

    auto p = freeList_.back().first;
    if (!freeList_.back().second) {
      PCHECK(0 == ::mprotect(p, pagesize() * guardPagesPerStack_, PROT_NONE));
      protectedRanges().wlock()->insert(std::make_pair(
          reinterpret_cast<intptr_t>(p),
          reinterpret_cast<intptr_t>(p + pagesize() * guardPagesPerStack_)));
    }
    freeList_.pop_back();

    /* We allocate minimum number of pages required, plus a guard page.
       Since we use this for stack storage, requested allocation is aligned
       at the top of the allocated pages, while the guard page is at the bottom.

               -- increasing addresses -->
             Guard page     Normal pages
            |xxxxxxxxxx|..........|..........|
            <- allocSize_ ------------------->
         p -^                <- size -------->
                      limit -^
    */
    auto limit = p + allocSize_ - size;
    assert(limit >= p + pagesize() * guardPagesPerStack_);
    return limit;
  }

  bool giveBack(unsigned char* limit, size_t size) {
    std::lock_guard<folly::SpinLock> lg(lock_);

    assert(storage_);

    auto as = allocSize(size, guardPagesPerStack_);
    if (std::less_equal<void*>{}(limit, storage_) ||
        std::less_equal<void*>{}(storage_ + allocSize_ * kNumGuarded, limit)) {
      /* not mine */
      return false;
    }

    auto p = limit + size - as;
    assert(as == allocSize_);
    assert((p - storage_) % allocSize_ == 0);
    freeList_.emplace_back(p, /* protected= */ true);
    return true;
  }

  ~StackCache() {
    assert(storage_);
    protectedRanges().withWLock([&](auto& ranges) {
      for (const auto& item : freeList_) {
        ranges.erase(std::make_pair(
            reinterpret_cast<intptr_t>(item.first),
            reinterpret_cast<intptr_t>(
                item.first + pagesize() * guardPagesPerStack_)));
      }
    });
    PCHECK(0 == ::munmap(storage_, allocSize_ * kNumGuarded));
  }

  static bool isProtected(intptr_t addr) {
    // Use a read lock for reading.
    return protectedRanges().withRLock([&](auto const& ranges) {
      for (const auto& range : ranges) {
        if (range.first <= addr && addr < range.second) {
          return true;
        }
      }
      return false;
    });
  }

 private:
  folly::SpinLock lock_;
  unsigned char* storage_{nullptr};
  const size_t allocSize_{0};
  const size_t guardPagesPerStack_{0};

  /**
   * LIFO free list. Each pair contains stack pointer and protected flag.
   */
  std::vector<std::pair<unsigned char*, bool>> freeList_;

  static size_t pagesize() {
    static const auto pagesize = size_t(sysconf(_SC_PAGESIZE));
    return pagesize;
  }

  /**
   * Returns a multiple of pagesize() enough to store size + a few guard pages
   */
  static size_t allocSize(size_t size, size_t guardPages) {
    return pagesize() * ((size + pagesize() * guardPages - 1) / pagesize() + 1);
  }

  /**
   * For each [b, e) range in this set, the bytes in the range were mprotected.
   */
  static folly::Synchronized<std::unordered_set<std::pair<intptr_t, intptr_t>>>&
  protectedRanges() {
    static auto instance = new folly::Synchronized<
        std::unordered_set<std::pair<intptr_t, intptr_t>>>();
    return *instance;
  }
};

#ifndef _WIN32

namespace {

struct sigaction oldSigsegvAction;

FOLLY_NOINLINE void FOLLY_FIBERS_STACK_OVERFLOW_DETECTED(
    int signum, siginfo_t* info, void* ucontext) {
  std::cerr << "folly::fibers Fiber stack overflow detected." << std::endl;
  // Let the old signal handler handle the signal, but make this function name
  // present in the stack trace.
  if (oldSigsegvAction.sa_flags & SA_SIGINFO) {
    oldSigsegvAction.sa_sigaction(signum, info, ucontext);
  } else {
    oldSigsegvAction.sa_handler(signum);
  }
  // Prevent tail call optimization.
  std::cerr << "";
}

void sigsegvSignalHandler(int signum, siginfo_t* info, void* ucontext) {
  // Restore old signal handler
  sigaction(signum, &oldSigsegvAction, nullptr);

  if (signum != SIGSEGV) {
    std::cerr << "GuardPageAllocator signal handler called for signal: "
              << signum;
    return;
  }

  if (info &&
      StackCache::isProtected(reinterpret_cast<intptr_t>(info->si_addr))) {
    FOLLY_FIBERS_STACK_OVERFLOW_DETECTED(signum, info, ucontext);
    return;
  }

  // Let the old signal handler handle the signal. Invoke this synchronously
  // within our own signal handler to ensure that the kernel siginfo context
  // is not lost.
  if (oldSigsegvAction.sa_flags & SA_SIGINFO) {
    oldSigsegvAction.sa_sigaction(signum, info, ucontext);
  } else {
    oldSigsegvAction.sa_handler(signum);
  }
}

bool isInJVM() {
  auto getCreated = dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
  return getCreated;
}

void installSignalHandler() {
  static std::once_flag onceFlag;
  std::call_once(onceFlag, []() {
    if (isInJVM()) {
      // Don't install signal handler, since JVM internal signal handler doesn't
      // work with SA_ONSTACK
      return;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    // By default signal handlers are run on the signaled thread's stack.
    // In case of stack overflow running the SIGSEGV signal handler on
    // the same stack leads to another SIGSEGV and crashes the program.
    // Use SA_ONSTACK, so alternate stack is used (only if configured via
    // sigaltstack).
    sa.sa_flags |= SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = &sigsegvSignalHandler;
    sigaction(SIGSEGV, &sa, &oldSigsegvAction);
  });
}
} // namespace

#endif

/*
 * RAII Wrapper around a StackCache that calls
 * CacheManager::giveBack() on destruction.
 */
class StackCacheEntry {
 public:
  explicit StackCacheEntry(size_t stackSize, size_t guardPagesPerStack)
      : stackCache_(
            std::make_unique<StackCache>(stackSize, guardPagesPerStack)) {}

  StackCache& cache() const noexcept { return *stackCache_; }

  ~StackCacheEntry();

 private:
  std::unique_ptr<StackCache> stackCache_;
};

class CacheManager {
 public:
  static CacheManager& instance() {
    static auto inst = new CacheManager();
    return *inst;
  }

  std::unique_ptr<StackCacheEntry> getStackCache(
      size_t stackSize, size_t guardPagesPerStack) {
    auto used = inUse_.load(std::memory_order_relaxed);
    do {
      if (used >= kMaxInUse) {
        return nullptr;
      }
    } while (!inUse_.compare_exchange_weak(
        used, used + 1, std::memory_order_acquire, std::memory_order_relaxed));
    return std::make_unique<StackCacheEntry>(stackSize, guardPagesPerStack);
  }

 private:
  std::atomic<size_t> inUse_{0};

  friend class StackCacheEntry;

  void giveBack(std::unique_ptr<StackCache> /* stackCache_ */) {
    [[maybe_unused]] auto wasUsed =
        inUse_.fetch_sub(1, std::memory_order_release);
    assert(wasUsed > 0);
    /* Note: we can add a free list for each size bucket
       if stack re-use is important.
       In this case this needs to be a folly::Singleton
       to make sure the free list is cleaned up on fork.

       TODO(t7351705): fix Singleton destruction order
    */
  }
};

StackCacheEntry::~StackCacheEntry() {
  CacheManager::instance().giveBack(std::move(stackCache_));
}

GuardPageAllocator::GuardPageAllocator(size_t guardPagesPerStack)
    : guardPagesPerStack_(guardPagesPerStack) {
#ifndef _WIN32
  installSignalHandler();
#endif
}

GuardPageAllocator::~GuardPageAllocator() = default;

unsigned char* GuardPageAllocator::allocate(size_t size) {
  if (guardPagesPerStack_ && !stackCache_) {
    stackCache_ =
        CacheManager::instance().getStackCache(size, guardPagesPerStack_);
  }

  if (stackCache_) {
    auto p = stackCache_->cache().borrow(size);
    if (p != nullptr) {
      return p;
    }
  }
  return fallbackAllocator_.allocate(size);
}

void GuardPageAllocator::deallocate(unsigned char* limit, size_t size) {
  if (!(stackCache_ && stackCache_->cache().giveBack(limit, size))) {
    fallbackAllocator_.deallocate(limit, size);
  }
}
} // namespace fibers
} // namespace folly
